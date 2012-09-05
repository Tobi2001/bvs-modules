#include "bvsStereoCUDA.h"



// This is your module's constructor.
// Please do not change its signature as it is called by the framework (so the
// framework actually creates your module) and the framework assigns the unique
// identifier and gives you access to its config.
// However, you should use it to create your data structures etc.
bvsStereoCUDA::bvsStereoCUDA(const std::string id, const BVS::Info& bvs)
	: BVS::Module(),
	id(id),
	logger(id),
	config("bvsStereoCUDA", 0, nullptr), // "bvsStereoCUDAConfig.txt"),
	// at this point config has already loaded 'bvsStereoCUDAConfig.txt", so
	// you can use config to retrieve settings in the initialization list, e.g.
	// yourSwitch(config.getValue<bool>(id + ".yourSwitch, false));
	bvs(bvs),
	input0("input0", BVS::ConnectorType::INPUT),
	input1("input1", BVS::ConnectorType::INPUT),
	depthImage("depthImage", BVS::ConnectorType::OUTPUT),
	in0(),
	in1(),
	grey0(),
	grey1(),
	gpuMat0(),
	gpuMat1(),
	disparity(),
	stereoAlgo(0),
	bmGPU(),
	bpGPU(),
	csGPU()
{
	bmGPU.preset = 0;
	bmGPU.ndisp = 64;
	bmGPU.winSize = 19;
	bmGPU.avergeTexThreshold = 2;

	bpGPU.ndisp = 64;
	bpGPU.iters = 5;
	bpGPU.levels = 5;
	bpGPU.msg_type = CV_32F;

	csGPU.ndisp = 128;
	csGPU.iters = 8;
	csGPU.levels = 4;
	csGPU.nr_plane = 4;
}



// This is your module's destructor.
// See the constructor for more info.
bvsStereoCUDA::~bvsStereoCUDA()
{

}



// Put all your work here.
BVS::Status bvsStereoCUDA::execute()
{
	if (!input0.receive(in0) || !input1.receive(in1)) return BVS::Status::NOINPUT;

	cv::pyrDown(in0, grey0, cv::Size(in0.cols/2, in0.rows/2));
	cv::pyrDown(in1, grey1, cv::Size(in1.cols/2, in1.rows/2));
	in0 = grey0;
	in1 = grey1;

	//LOG(0, cv::gpu::StereoBM_GPU::checkIfGpuCallReasonable());
	if (in0.type()!=CV_8UC1 || in1.type()!=CV_8UC1)
	{
		cv::cvtColor(in0, grey0, CV_RGB2GRAY);
		cv::cvtColor(in1, grey1, CV_RGB2GRAY);
	}

	gpuMat0.upload(grey0);
	gpuMat1.upload(grey1);

	//TODO try
	//bpGPU.estimateRecommendedParams(...)
	//csGPU.estimateRecommendedParams(...)
	//cv::gpu::DisparityBilateralFilter()

	switch (stereoAlgo)
	{
		case 0: bmGPU(gpuMat0, gpuMat1, disparity); break;
		case 1: bpGPU(gpuMat0, gpuMat1, disparity); break;
		case 2: csGPU(gpuMat0, gpuMat1, disparity); break;
	}

	cv::Mat grey;
	disparity.download(grey);
	//TODO needed? grey.convertTo(grey, CV_8U, 1./16.);
	cv::imshow("grey", grey);

	cv::Mat out;
	cv::gpu::GpuMat color;
	cv::gpu::drawColorDisp(disparity, color, bmGPU.ndisp);
	color.download(out);
	cv::putText(out, bvs.getFPS(), cv::Point(10, 30),
			CV_FONT_HERSHEY_SIMPLEX, 1.0f, cvScalar(0, 0, 255), 2);
	cv::imshow("out", out);

	char c = cv::waitKey(1);
	switch (c)
	{
		case 'h':
			LOG(1, "\n\
usage:\n\
    a   - change algorithm: " << (stereoAlgo==0? "BM": stereoAlgo==1? "BP": "CSBP") << "\n\
BM:\n\
    p   - prefilter on/off: " << bmGPU.preset << "\n\
    n/N - change number of disparities: " << bmGPU.ndisp << "\n\
    w/W - change window size: " << bmGPU.winSize << "\n\
    t/T - change texture averaging threshold: " << bmGPU.avergeTexThreshold << "\n\
BP:\n\
    n/N - change number of disparities: " << bpGPU.ndisp << "\n\
    i/I - change number of iterations " << bpGPU.iters << "\n\
    l/L - change number of levels " << bpGPU.levels << "\n\
CS:\n\
    n/N - change number of disparities: " << csGPU.ndisp << "\n\
    i/I - change number of iterations " << csGPU.iters << "\n\
    l/L - change number of levels " << csGPU.levels << "\n\
"); break;

		case 'a':
			stereoAlgo = (stereoAlgo + 1) % 3;
			LOG(1, "algo: " << (stereoAlgo==0? "BM": stereoAlgo==1? "BP": "CSBP"));
			break;
		case 'p':
			bmGPU.preset = (bmGPU.preset + 1) % 2;
			LOG(1, "preset: " << bmGPU.preset);
			break;
		case 'N':
			switch (stereoAlgo)
			{
				case 0: bmGPU.ndisp += 8; LOG(1, "ndisp: " << bmGPU.ndisp); break;
				case 1: bpGPU.ndisp += 8; LOG(1, "ndisp: " << bpGPU.ndisp); break;
				case 2: csGPU.ndisp += 8; LOG(1, "ndisp: " << csGPU.ndisp); break;
			}
			break;
		case 'n':
			switch (stereoAlgo)
			{
				case 0: bmGPU.ndisp = bmGPU.ndisp-8 < 8 ? 8: bmGPU.ndisp-8; LOG(1, "ndisp: " << bmGPU.ndisp); break;
				case 1: bpGPU.ndisp = bpGPU.ndisp-8 < 8 ? 8: bpGPU.ndisp-8; LOG(1, "ndisp: " << bpGPU.ndisp); break;
				case 2: csGPU.ndisp = csGPU.ndisp-8 < 8 ? 8: csGPU.ndisp-8; LOG(1, "ndisp: " << csGPU.ndisp); break;
			}
			break;
		case'W':
			bmGPU.winSize += 1;
			LOG(1, "winSize: " << bmGPU.winSize);
			break;
		case'w':
			bmGPU.winSize -= 1;
			bmGPU.winSize < 2 ? bmGPU.winSize = 2: bmGPU.winSize;
			LOG(1, "winSize: " << bmGPU.winSize);
			break;
		case'T':
			bmGPU.avergeTexThreshold += 1;
			LOG(1, "tex: " << bmGPU.avergeTexThreshold);
			break;
		case't':
			bmGPU.avergeTexThreshold -= 1;
			bmGPU.avergeTexThreshold < 0 ? bmGPU.avergeTexThreshold = 0: bmGPU.avergeTexThreshold;
			LOG(1, "tex: " << bmGPU.avergeTexThreshold);
			break;
		case 'I':
			switch (stereoAlgo)
			{
				case 1: bpGPU.iters += 1; LOG(1, "iters: " << bpGPU.iters); break;
				case 2: csGPU.iters += 1; LOG(1, "iters: " << csGPU.iters); break;
			}
			break;
		case 'i':
			switch (stereoAlgo)
			{
				case 1: bpGPU.iters = bpGPU.iters-1 < 1 ? 1: bpGPU.iters-1; LOG(1, "iters: " << bpGPU.iters); break;
				case 2: csGPU.iters = csGPU.iters-1 < 1 ? 1: csGPU.iters-1; LOG(1, "iters: " << csGPU.iters); break;
			}
			break;
		case 'L':
			switch (stereoAlgo)
			{
				case 1: bpGPU.levels += 1; LOG(1, "levels: " << bpGPU.levels); break;
				case 2: csGPU.levels += 1; LOG(1, "levels: " << csGPU.levels); break;
			}
			break;
		case 'l':
			switch (stereoAlgo)
			{
				case 1: bpGPU.levels = bpGPU.levels-1 < 1 ? 1: bpGPU.levels-1; LOG(1, "levels: " << bpGPU.levels); break;
				case 2: csGPU.levels = csGPU.levels-1 < 1 ? 1: csGPU.levels-1; LOG(1, "levels: " << csGPU.levels); break;
			}
			break;
		case 27: exit (0); break;
	}


	return BVS::Status::OK;
}



// UNUSED
BVS::Status bvsStereoCUDA::debugDisplay()
{
	return BVS::Status::OK;
}



// This function is called by the framework upon creating a module instance of
// this class. It creates the module and registers it within the framework.
// DO NOT CHANGE OR DELETE
extern "C" {
	int bvsRegisterModule(std::string id, BVS::Info& bvs)
	{
		registerModule(id, new bvsStereoCUDA(id, bvs));

		return 0;
	}
}
