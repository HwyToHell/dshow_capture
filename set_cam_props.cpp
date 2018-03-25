
void setCamProps() {
	using namespace std;

	// Initialize COM
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if(FAILED(hr)) {
        cerr << "CoInitialize Failed!" << endl;   
		throw "com error";
    } 

	// create helper for capture filter graph
	ICaptureGraphBuilder2* pBuild = NULL; // released
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, 
			NULL, 
			CLSCTX_INPROC_SERVER,
			IID_ICaptureGraphBuilder2,
			(void**)&pBuild );

	// create and set filter graph via graph builder
	IGraphBuilder* pGraph = NULL; // released
	hr = CoCreateInstance(CLSID_FilterGraph,
			NULL,
			CLSCTX_INPROC_SERVER,
            IID_IGraphBuilder,
			(void**)&pGraph);
	if (FAILED(hr)) {
		cerr << "cannot instantiate filter graph" << endl;
		throw "com error";
	}
	hr = pBuild->SetFiltergraph(pGraph);

	// enumerate devices
	ICreateDevEnum* pDevEnum = NULL; // released
    hr = CoCreateInstance (CLSID_SystemDeviceEnum,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_ICreateDevEnum,
			(void **) &pDevEnum);

    IEnumMoniker *pClassEnum = NULL; // released
	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
			&pClassEnum,
			0);

	// enumerate capture devices to list
	struct TCapDev {
		IMoniker* pMoniker;
		wstring name;
	} capDev;
	list<TCapDev> capDevList;
	IMoniker* pMoniker = NULL; // released

	IPropertyBag* pPropBag; // released
	while (S_OK == pClassEnum->Next(1, &pMoniker, NULL)) {
		capDev.pMoniker = pMoniker;

		// bind prop bag for friendly name

		hr = pMoniker->BindToStorage(NULL, NULL, IID_IPropertyBag, (void**)&pPropBag);
		
		// get friendly name
		VARIANT var;
		VariantInit(&var);
		hr = pPropBag->Read(L"FriendlyName", &var, 0);
		wcout << var.bstrVal << endl;
		capDev.name = wstring(var.bstrVal);

		capDevList.push_back(capDev);
	}
	pPropBag->Release();

	// take first device
	pMoniker = capDevList.front().pMoniker;
	
	// bind moniker to filter object
    IBaseFilter *pCapSrc = NULL;  // released
	hr = pMoniker->BindToObject(0,0,IID_IBaseFilter, (void**)&pCapSrc);
	if (FAILED(hr)) {
		cerr << "unable to access video capture device" << endl;   
		throw "com error";
	}


	// add filter object to filter graph
	hr = pGraph->AddFilter(pCapSrc, L"Capture Filter");
	
	// enumerate pins to list
	IEnumPins* pPinEnum; // released
	hr = pCapSrc->EnumPins(&pPinEnum);

	IPin* pPin; // released
	LPWSTR szID = NULL;
	list<IPin*> pinList;
	PIN_INFO pinInfo;

	// pin ID (should be "0" for first cam)
	while (S_OK == pPinEnum->Next(1, &pPin, NULL)) {
		pinList.push_back(pPin);
		hr = pPin->QueryPinInfo(&pinInfo);

		hr = pPin->QueryId(&szID);
		wstring id(szID);
		CoTaskMemFree(szID);
	}

	// set format with stream info interface
	IAMStreamConfig* pStreamCfg; // released
	hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pStreamCfg);
	
	int nCaps;
	int nSize;
	hr = pStreamCfg->GetNumberOfCapabilities(&nCaps, &nSize);

	list<AM_MEDIA_TYPE> capsList;
	for (int i = 0; i < nCaps; ++i) {
		AM_MEDIA_TYPE* pMediaType;
		VIDEO_STREAM_CONFIG_CAPS streamCaps;
		hr = pStreamCfg->GetStreamCaps(i, &pMediaType, (BYTE*)&streamCaps);
		capsList.push_back(*pMediaType);

		GUID majorType;
		majorType = pMediaType->majortype;
		GUID subType;
		subType = pMediaType->subtype;
	}

	// set frame size via videoinfoheader
	AM_MEDIA_TYPE* pMediaType;
	hr = pStreamCfg->GetFormat(&pMediaType);

	GUID formatType(FORMAT_None);
	formatType = pMediaType->formattype;
	if (formatType != FORMAT_VideoInfo) {
		cout << "wrong video format for capture device" << endl;
	}
	else { 
		VIDEOINFOHEADER* pVideoInfo = (VIDEOINFOHEADER*)pMediaType->pbFormat;
		// frame size before change
		int width = pVideoInfo->bmiHeader.biWidth;
		int height = pVideoInfo->bmiHeader.biHeight;
		
		pVideoInfo->bmiHeader.biWidth = 320;
		pVideoInfo->bmiHeader.biHeight = 240;
		pStreamCfg->SetFormat(pMediaType);

	}

	IMediaControl* pControl = NULL;
	hr = pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
	IMediaEvent* pEvent = NULL;
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void**)&pEvent);

	hr = pBuild->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pCapSrc, 
		NULL, NULL);

	hr = pControl->Run();

	long evCode;
	hr = pEvent->WaitForCompletion(INFINITE, &evCode);

	pStreamCfg->Release();
	pPinEnum->Release();
	pPin->Release();
	pMoniker->Release();
	pCapSrc->Release();
	pBuild->Release();
	pGraph->Release();

	CoUninitialize();
}