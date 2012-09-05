#ifndef BVSSTEREOCUDA_H
#define BVSSTEREOCUDA_H

#include "bvs/module.h"
#include "opencv2/opencv.hpp"
#include "opencv2/gpu/gpu.hpp"



/** This is the bvsStereoCUDA class.
 * Please add sufficient documentation to enable others to use it.
 * Include information about:
 * - Dependencies
 * - Inputs
 * - Outputs
 * - Configuration Options
 */
class bvsStereoCUDA : public BVS::Module
{
	public:
		/** Your module constructor.
		 * Please do not change the signature, as it will be called by the
		 * framework.
		 * You can use the constructor/destructor pair to create/destroy your data.
		 * @param[in] id Your modules unique identifier, will be set by framework.
		 * @param[in] bvs Reference to framework info for e.g. config option retrieval.
		 */
		bvsStereoCUDA(const std::string id, const BVS::Info& bvs);

		/** Your module destructor. */
		~bvsStereoCUDA();

		/** Execute function doing all the work.
		 * This function is executed exactly once and only once upon each started
		 * round/step of the framework. It is supposed to contain the actual work
		 * of your module.
		 */
		BVS::Status execute();

		/** UNUSED
		 * @return Module's status.
		 */
		BVS::Status debugDisplay();

	private:
		const std::string id; /**< Your unique module id, set by framework. */

		/** Your logger instance.
		 * @see Logger
		 */
		BVS::Logger logger;

		/** Your config system.
		 * @see Config
		 */
		BVS::Config config;

		/** Your Info recerence;
		 * @see Info
		 */
		const BVS::Info& bvs;

		/** Example Connector used to retrieve/send data from/to other modules.
		 * @see Connector
		 */
		BVS::Connector<cv::Mat> input0;
		BVS::Connector<cv::Mat> input1;
		BVS::Connector<cv::Mat> depthImage;

		cv::Mat in0;
		cv::Mat in1;
		cv::Mat grey0;
		cv::Mat grey1;

		cv::gpu::GpuMat gpuMat0;
		cv::gpu::GpuMat gpuMat1;
		cv::gpu::GpuMat disparity;

		int stereoAlgo;
		cv::gpu::StereoBM_GPU bmGPU;
		cv::gpu::StereoBeliefPropagation bpGPU;
		cv::gpu::StereoConstantSpaceBP csGPU;

		bvsStereoCUDA(const bvsStereoCUDA&) = delete; /**< -Weffc++ */
		bvsStereoCUDA& operator=(const bvsStereoCUDA&) = delete; /**< -Weffc++ */
};

#endif //BVSSTEREOCUDA_H
