// cam capture class based on direct show
// use alternatively for OpenCV VideoCapture class
// if set frame size does not work


#include <string>
#include <vector>
#include <DShow.h>
#include <opencv2/opencv.hpp>
#include "qedit.h" // sample grabber

/* TODO delete enum and include opencv
enum {
	CV_CAP_PROP_FRAME_WIDTH    =3,
    CV_CAP_PROP_FRAME_HEIGHT   =4,
};
*/

void cpyFlipHori(BYTE dst[], const BYTE src[], long height, long width, int nByteDepth);

struct CamDevice { 
	std::wstring name;
	IMoniker* pMoniker;
};

struct StreamCaps {
	AM_MEDIA_TYPE mediaType;
	BITMAPINFOHEADER bmiHeader;
};

class SampleGrabberCallback;

class CamInput {
private:
	// 
	std::vector<CamDevice> m_deviceArray;
	std::vector<StreamCaps> m_streamCapsArray;

	ICaptureGraphBuilder2* m_captureBuilder;
	IGraphBuilder* m_graphBuilder;

	ISampleGrabber* m_sampleGrabber;
	SampleGrabberCallback* m_sGrabCallBack;

	IMediaControl* m_mediaControl;
	IVideoWindow* m_videoWindow;

	IBaseFilter* m_camSrcFilter;
	IBaseFilter* m_grabFilter;
	IBaseFilter* m_renderFilter;

	DWORD m_registerRot;

	//
	bool addCamSrcFilter(int capDeviceNum, IGraphBuilder* pGraph);
	bool addGrabFilter(IGraphBuilder* pGraph);
	bool addNullRenderFilter(IGraphBuilder* pGraph);

public:
	CamInput();
	~CamInput();

	int enumerateDevices();
	int enumerateStreamCaps();
	double get(int propID);
	bool initGraph(int capDeviceNum);
	void printStreamCaps();
	bool read(cv::Mat& bitmap);
	bool runGraph();
	bool setResolution(int capabilityID);
	bool stopGraph();
};
