#include "bvsCalibration.h"
#include "sys/stat.h"



bvsCalibration::bvsCalibration(const std::string id, const BVS::Info& bvs)
	: BVS::Module(),
	id(id),
	logger(id),
	bvs(bvs),
	config("bvsCalibration", 0, nullptr, "bvsCalibrationConfig.txt"),
	numNodes(config.getValue<int>(id + ".numNodes", 0)),
	numImages(config.getValue<int>(id + ".numImages", 100)),
	circleSize(config.getValue<float>(id + ".circleSize", 2.42f)),
	autoShotMode(config.getValue<bool>(id + ".autoShotMode", true)),
	autoShotDelay(config.getValue<int>(id + ".autoShotDelay", 1)),
	directory(config.getValue<std::string>(id + ".directory", "calibrationData")),
	saveImages(config.getValue<bool>(id + ".saveImages", false)),
	useSavedImages(config.getValue<bool>(id + ".useSavedImages", false)),
	loadCalibration(config.getValue<bool>(id + ".loadCalibration", true)),
	saveCalibration(config.getValue<bool>(id + ".saveCalibration", true)),
	calibrationFile(config.getValue<std::string>(id + ".calibrationFile", "calibration.xml")),
	createRectifiedOutput(config.getValue<bool>(id + ".createRectifiedOutput", true)),
	addGridOverlay(config.getValue<bool>(id + ".addGridOverlay", false)),
	useCalibrationGuide(config.getValue<bool>(id + ".useCalibrationGuide", false)),
	centerScale(config.getValue<float>(id + ".centerScale", 0.5)),
	centerDetections(config.getValue<int>(id + ".centerDetections", 60)),
	sectorDetections(config.getValue<int>(id + ".sectorDetections", 5)),
	calibrated(false),
	detectionRunning(false),
	numDetections(0),
	objectPoints(),
	imageSize(),
	boardSize(4, 11),
	detectionThread(),
	detectionMutex(),
	detectionLock(detectionMutex),
	detectionCond(),
	shotTimer(std::chrono::high_resolution_clock::now()),
	reflectX(),
	reflectY(),
	nodes(),
	stereo(nodes),
	guide(numImages, numDetections, centerScale, centerDetections, sectorDetections)
{
	for (int i=0; i<numNodes; i++)
	{
		nodes.emplace_back(new CalibrationNode(
				i,
				BVS::Connector<cv::Mat>(std::string("input") + std::to_string(i), BVS::ConnectorType::INPUT),
				BVS::Connector<cv::Mat>(std::string("output") + std::to_string(i), BVS::ConnectorType::OUTPUT),
				cv::Mat(), cv::Mat(), std::vector<cv::Point2f>(), cv::Mat(),
				std::vector<cv::Point2f>(), std::vector<std::vector<cv::Point2f>>(),
				cv::Mat::eye(3, 3, CV_64F), cv::Mat(), cv::Mat(), cv::Mat(), cv::Mat(), cv::Rect()));
	}

	struct stat *buf = nullptr;
	if (stat(directory.c_str(), buf)) mkdir(directory.c_str(), 0755);

	if (loadCalibration) calibrated = loadCalibrationFrom(directory, calibrationFile);
	if (!calibrated)
	{
		detectionThread = std::thread(&bvsCalibration::detectCalibrationPoints, this);
		detectionThread.detach();
	}
}



bvsCalibration::~bvsCalibration()
{
	for (auto& it: nodes)
	{
		delete it;
	}
}



BVS::Status bvsCalibration::execute()
{
	for (auto& node: nodes)
		if(!useSavedImages && !node->input.receive(node->frame)) return BVS::Status::NOINPUT;

	if (imageSize == cv::Size())
	{
		imageSize = nodes[0]->frame.size();
		generateReflectionMap();
	}

	for (auto& node: nodes)
		if(!calibrated) cv::remap(node->frame, *node->output, reflectX, reflectY, 0);

	if (!calibrated && numDetections<numImages) collectCalibrationImages();
	if (!calibrated && numDetections==numImages && !detectionRunning)
	{
		if (useCalibrationGuide)
		{
			guide.reorderDetections(nodes[0]->pointStore);
			guide.reorderDetections(nodes[1]->pointStore);
		}
		calibrate();
		clearCalibrationData();
		calibrated = true;
	}

	if (calibrated && createRectifiedOutput && !useSavedImages) rectifyOutput(addGridOverlay);

	static double avgFPS = 15;
	// apply exponential smoothing with alpha = 0.2
	double duration = bvs.lastRoundDuration.count();
	duration = duration > 1000 ? 1000 : duration;
	avgFPS = (1000/duration + 4 * avgFPS)/5;
	std::string fps = std::to_string(avgFPS);
	fps.resize(fps.length()-5);

	if (!calibrated && useCalibrationGuide) guide.addTargetOverlay(*nodes[0]->output);
	for (auto& node: nodes)
	{
		cv::putText(*node->output,fps, cv::Point(10, 30),
				CV_FONT_HERSHEY_SIMPLEX, 1.0f, cvScalar(0, 0, 255), 2);
		if (calibrated)
		{
		}
		else
		{
			cv::putText(*node->output, std::to_string(numDetections) + "/" + std::to_string(numImages),
					cv::Point(100, 30), CV_FONT_HERSHEY_SIMPLEX, 1.0f, cv::Scalar(0, 255, 0), 2, 8);
		}

		if (!useSavedImages) cv::imshow(std::to_string(node->id), *node->output);
		//cv::moveWindow(std::to_string(node->id), node->id*node->output->cols, 0);
	}

	if (false) //(calibrated) //TODO make 'headless' mode to calibrate offline
	{
		static int i = 1;
		LOG(1, "rectifying image " << i);
		nodes[0]->frame = cv::imread(std::string(directory + "/img") + std::to_string(i)
					+ "-0.pbm");
		nodes[1]->frame = cv::imread(std::string(directory + "/img") + std::to_string(i)
					+ "-1.pbm");
		rectifyOutput(addGridOverlay);
		cv::imwrite(directory + "/test" + std::to_string(i) + "-0.jpg", *nodes[0]->output);
		cv::imwrite(directory + "/test" + std::to_string(i) + "-1.jpg", *nodes[1]->output);
		i++;
		if (i>numImages) exit(0);
	}

	if (!useSavedImages)
	{
		char c = cv::waitKey(1);
		if (c==27) exit(0);
		if (!autoShotMode && c==' ') notifyDetectionThread();
	}

	return BVS::Status::OK;
}



// UNUSED
BVS::Status bvsCalibration::debugDisplay()
{
	return BVS::Status::OK;
}



extern "C" {
	int bvsRegisterModule(std::string id, const BVS::Info& bvs)
	{
		registerModule(id, new bvsCalibration(id, bvs));

		return 0;
	}
}



bool bvsCalibration::loadCalibrationFrom(const std::string& directory, const std::string& file)
{
	switch (numNodes)
	{
		case 1:
			// single camera
			return false;
			break;
		case 2:
			// stereo
			return stereo.loadFromFile(directory, file);
			break;
	}
	return false;
}



bool bvsCalibration::saveCalibrationTo(const std::string& directory, const std::string& file)
{
	switch (numNodes)
	{
		case 1:
			// single camera
			return false;
			break;
		case 2:
			// stereo
			return stereo.saveToFile(directory, file);
			break;
	}
	return false;
}



void bvsCalibration::generateReflectionMap()
{
	cv::Mat mapX;
	cv::Mat mapY;
	mapX.create(imageSize, CV_32FC1);
	mapY.create(imageSize, CV_32FC1);
	for (int i=0; i<imageSize.height; i++)
		for (int j=0; j<imageSize.width; j++)
		{
			mapX.at<float>(i,j) = imageSize.width-j;
			mapY.at<float>(i,j) = i;
		}
	cv::convertMaps(mapX, mapY, reflectX, reflectY, CV_16SC2);
}



void bvsCalibration::calibrate()
{
	switch (numNodes)
	{
		case 1:
			// single camera
			break;
		case 2:
			stereo.calibrate(numDetections, imageSize, boardSize, circleSize);
			if (saveCalibration) saveCalibrationTo(directory, calibrationFile);
			break;
	}
}



void bvsCalibration::rectifyOutput(bool addGridOverlay)
{
	switch (numNodes)
	{
		case 1:
			// single camera
			break;
		case 2:
			stereo.rectify(addGridOverlay);
			break;
	}
}



void bvsCalibration::collectCalibrationImages()
{
	bool foundPattern = false;
	int numPositives = 0;

	if (useSavedImages && numDetections<=numImages)
	{
		for (auto& node: nodes)
		{
			node->frame = cv::imread(std::string(directory + "/img") + std::to_string(numDetections+1)
					+ "-" + std::to_string(node->id) + ".pbm");
			if (node->frame.empty())
			{
				LOG(0, "NOT FOUND: " << std::string(directory + "/img") + std::to_string(numDetections+1)
					+ "-" + std::to_string(node->id) + ".pbm");
				exit(1);
			}
		}
		notifyDetectionThread();
		return;
	}

	for (auto& node: nodes)
	{
		cv::pyrDown(node->frame, node->scaledFrame, cv::Size(imageSize.width/2, imageSize.height/2));
		foundPattern = cv::findCirclesGrid(node->scaledFrame, boardSize,
				node->framePoints, cv::CALIB_CB_ASYMMETRIC_GRID);
		for (auto& point: node->framePoints)
		{
			point.x = node->frame.cols - point.x * 2;
			point.y = point.y * 2;
		}
		cv::drawChessboardCorners(*node->output, boardSize,
				cv::Mat(node->framePoints), foundPattern);

		if (!foundPattern) break;
		if(foundPattern) numPositives++;
		if (numPositives==numNodes)
		{
			if (!autoShotMode) return;
			if (useCalibrationGuide)
				if (!guide.checkDetectionQuality(*nodes[0]->output, nodes[0]->framePoints))
					return;
			notifyDetectionThread();
		}
	}
}



void bvsCalibration::notifyDetectionThread()
{
	if (detectionRunning) return;

	if (autoShotMode && std::chrono::duration_cast<std::chrono::seconds>
			(std::chrono::high_resolution_clock::now() - shotTimer).count() < autoShotDelay && !useSavedImages)
		return;

	if (useSavedImages) LOG(1, "loading image: " << numDetections+1 << "/" << numImages);
	
	shotTimer = std::chrono::high_resolution_clock::now();
	numDetections++;
	for (auto& node: nodes)
		node->sample = node->frame.clone();
	detectionRunning = true;
	detectionCond.notify_one();
}



void bvsCalibration::detectCalibrationPoints()
{
	bool foundPattern;
	int numPositives;

	for (auto& node: nodes)
		node->pointStore.resize(numImages);

	while (numDetections<numImages)
	{
		detectionCond.wait(detectionLock, [&](){ return detectionRunning; });
		numPositives = 0;
		for (auto& node: nodes)
		{
			foundPattern = cv::findCirclesGrid(node->sample, boardSize, node->points, cv::CALIB_CB_ASYMMETRIC_GRID);
			if (!foundPattern)
			{
				numDetections--;
				break;
			}
			else numPositives++;

			if (numPositives==numNodes)
			{
				for (auto& node: nodes)
				{
					node->pointStore.at(numDetections-1) = node->points;
					if (saveImages)
						cv::imwrite(std::string(directory + "/img") + std::to_string(numDetections)
								+ "-" + std::to_string(node->id) + ".pbm", node->sample);
				}
			}
		}
		detectionRunning = false;
	}
}



void bvsCalibration::clearCalibrationData()
{
	for (auto& node: nodes)
	{
		node->scaledFrame.release();
		node->framePoints.clear();
		node->framePoints.shrink_to_fit();
		node->sample.release();
		node->points.clear();
		node->points.shrink_to_fit();
		node->pointStore.clear();
		node->pointStore.shrink_to_fit();
	}
}

