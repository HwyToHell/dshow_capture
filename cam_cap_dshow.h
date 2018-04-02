/// cam capture class based on direct show
/// as an alternative to OpenCV VideoCapture class,
/// in case VideoCapture::set(CV_CAP_PROP_FRAME_XXXXX)
/// for setting frame size does not work

#include <string>
#include <vector>
#include <DShow.h>
#include <opencv2/opencv.hpp>
#include "qedit.h"				// sample grabber


// camera device with friendly name
struct CamDevice { 
	std::wstring name;
	IMoniker* pMoniker;
};

// stream capability with frame size in bmiHeader
struct StreamCaps {
	AM_MEDIA_TYPE mediaType;
	BITMAPINFOHEADER bmiHeader;
};

class SampleGrabberCallback;

// replacement for cv::VideoCapture
class CamInput {
private:
	std::vector<CamDevice>	m_deviceArray;
	std::vector<StreamCaps> m_streamCapsArray;		// frame size
	ICaptureGraphBuilder2*	m_captureBuilder;
	IGraphBuilder*			m_graphBuilder;
	ISampleGrabber*			m_sampleGrabber;
	SampleGrabberCallback*	m_sGrabCallBack;
	IMediaControl*			m_mediaControl;
	IVideoWindow*			m_videoWindow;
	IBaseFilter*			m_camSrcFilter;
	IBaseFilter*			m_grabFilter;
	IBaseFilter*			m_renderFilter;
	DWORD					m_registerRot;			// debugging with graphedt

	bool addCamSrcFilter(int deviceID, IGraphBuilder* pGraph);
	bool addGrabFilter(IGraphBuilder* pGraph);
	bool addNullRenderFilter(IGraphBuilder* pGraph);

public:
	CamInput();
	~CamInput();

	int				enumerateDevices();
	int				enumerateStreamCaps();
	double			get(int propID);				// get frame size for opencv compatibility
	std::wstring	getDevice(int deviceID);
	cv::Size		getResolution(int capabilityID);
	bool			isGraphRunning();
	void			printStreamCaps();
	bool			read(cv::Mat& bitmap);
	bool			runGraph();
	bool			setDevice(int deviceID);
	bool			setResolution(int capabilityID);
	bool			stopGraph();
};
