//
//  RecognitionCore.cpp
//  CardRecognizer
//
//  Created by Vladimir Tchernitski on 12/01/16.
//  Copyright © 2016 Vladimir Tchernitski. All rights reserved.
//

#include <thread>

#include "RecognitionCore.h"
#include "IServiceContainer.h"
#include "IFrameStorage.h"
#include "IEdgesDetector.h"
#include "ITorchDelegate.h"
#include "ITorchManager.h"

#include "Utils.h"

#define kDateRecognitionAttempts  5

static int dateRecognitionAttemptsCount = 0;

bool IRecognitionCore::GetInstance(shared_ptr<IRecognitionCore> &recognitionCore,
                                   const shared_ptr<ITorchDelegate>& torchDelegate)
{
    recognitionCore = make_shared<CRecognitionCore>(torchDelegate);
    return recognitionCore != 0;
}

CRecognitionCore::CRecognitionCore(const shared_ptr<ITorchDelegate>& torchDelegate) :  _orientation(CardsOrientationPortrait), _deployed(false)
{
    _isIdle.store(false);
    _isBusy.store(false);
    
    IServiceContainerFactory::CreateServiceContainer(_serviceContainerPtr);
    
    _serviceContainerPtr->Initialize();
    _frameStorage = _serviceContainerPtr->resolve<IFrameStorage>();
    _edgesDetector = _serviceContainerPtr->resolve<IEdgesDetector>();
    _torchManager = _serviceContainerPtr->resolve<ITorchManager>();
    
    if(auto torchManager = _torchManager.lock()) {
        torchManager->SetDelegate(torchDelegate);
    }
}

CRecognitionCore::~CRecognitionCore()
{
    _isIdle.store(false);
    _edges.clear();
    _currentFrame.release();
}

//void CRecognitionCore::SetRecognitionMode(PayCardsRecognizerMode flag)
//{
//    _mode = flag;
//
//    if (_mode&PayCardsRecognizerModeNumber) {
//        _numberRecognizer = _serviceContainerPtr->resolve<INumberRecognizer>();
//        if(auto numberRecognizer = _numberRecognizer.lock()) {
//            numberRecognizer->SetRecognitionMode(flag);
//        }
//    }
//    if (_mode&PayCardsRecognizerModeDate) {
//        _dateRecognizer = _serviceContainerPtr->resolve<IDateRecognizer>();
//        if(auto dateRecognizer = _dateRecognizer.lock()) {
//            dateRecognizer->SetRecognitionMode(flag);
//        }
//    }
//    if (_mode&PayCardsRecognizerModeName) {
//        _nameRecognizer = _serviceContainerPtr->resolve<INameRecognizer>();
//        if(auto nameRecognizer = _nameRecognizer.lock()) {
//            nameRecognizer->SetRecognitionMode(flag);
//        }
//    }
//}

void CRecognitionCore::Deploy()
{
//    if (_mode == PayCardsRecognizerModeNone) return;
//
//    if (auto numberRecognizer = _numberRecognizer.lock()) {
//        numberRecognizer->SetDelegate(_delegate);
//        if(!numberRecognizer->Deploy()) return;
//    }
//
//    if (auto dateRecognizer = _dateRecognizer.lock()) {
//        dateRecognizer->SetDelegate(_delegate);
//        if(!dateRecognizer->Deploy()) return;
//    }
//
//    if (auto nameRecognizer = _nameRecognizer.lock()) {
//        nameRecognizer->SetDelegate(_delegate);
//        if(!nameRecognizer->Deploy()) return;
//    }
    
    _deployed = true;
}

void CRecognitionCore::SetOrientation(CardsOrientation orientation)
{
    _orientation = orientation;
}

cv::Rect CRecognitionCore::CalcWorkingArea(cv::Size frameSize, int captureAreaWidth)
{
    if(auto edgesDetector = _edgesDetector.lock()) {
        return edgesDetector->CalcWorkingArea(frameSize, captureAreaWidth, _orientation);
    }
    
    return Rect(0,0,0,0);
}

void CRecognitionCore::SetIdle(bool isIdle)
{
//    if (!isIdle) {
//        if(auto recognitionResult = _recognitionResult.lock()) {
//            recognitionResult->Reset();
//        }
//    }
    
    _isIdle.store(isIdle);
}

bool CRecognitionCore::IsIdle() const
{
    return _isIdle.load();
}

void CRecognitionCore::ResetResult()
{
//    if(auto recognitionResult = _recognitionResult.lock()) {
//        recognitionResult->Reset();
//    }
}

void CRecognitionCore::ProcessFrame(DetectedLineFlags& edgeFlags, void* bufferY,
                                    void* bufferUV, size_t bufferSizeY, size_t bufferSizeUV)
{
    if (!IsIdle() && _deployed) {
        
        Mat frameMatY = cv::Mat(1280, 720, CV_8UC1, bufferY); //put buffer in open cv, no memory copied
        
        if(auto edgesDetector = _edgesDetector.lock()) {
//            cout << "DEBUG edges detect lock" << endl;
            if(auto frameStorage = _frameStorage.lock()) {
//                cout << "DEBUG frame storage lock" << endl;
                edgeFlags = edgesDetector->DetectEdges(frameMatY, _edges, _currentFrame);
//                if (edgeFlags&DetectedLineTopFlag &&  edgeFlags&DetectedLineBottomFlag) {
                if (edgeFlags&DetectedLineTopFlag &&  edgeFlags&DetectedLineBottomFlag
                    && edgeFlags&DetectedLineLeftFlag && edgeFlags&DetectedLineRightFlag) {
                    
                    SetTorchStatus(true);
                    cout << "DEBUG detect true" << endl;
//                    if (!_isBusy.load()) {
//                        _isBusy.store(true);
//
//                        _bufferSizeY = bufferSizeY;
//
//                        std::thread thread;
//                        thread = std::thread( [this] { this->ProcessFrameThreaded(); } );
//
//                        sched_param sch;
//                        int policy;
//                        pthread_getschedparam(thread.native_handle(), &policy, &sch);
//                        sch.sched_priority = 99;
//                        pthread_setschedparam(thread.native_handle(), SCHED_FIFO, &sch);
//
//                        thread.detach();
//                    }
                }
            }
        }
    }
}

void CRecognitionCore::FinishRecognition()
{
    if(auto frameStorage = _frameStorage.lock()) {
        frameStorage->PopFrame();
        _isBusy.store(false);
    }
}

void CRecognitionCore::ProcessFrameThreaded()
{
    if(auto frameStorage = _frameStorage.lock()) {
        if(frameStorage->SetRawFrame(_currentFrame, _edges, _orientation)) {
            Recognize();
        }
        else {
            _isBusy.store(false);
        }
    }
}

void CRecognitionCore::Recognize()
{
//    if (auto recognitionResult = _recognitionResult.lock()) {
    
//        if (!RecognizeNumber()) {
//            FinishRecognition();
//            return;
//        }
        
    RecognizeNumber();
    
    _isIdle.store(true);
    FinishRecognition();
    dateRecognitionAttemptsCount = 0;
    SetTorchStatus(false);
//    }
}

void CRecognitionCore::SetTorchStatus(bool status)
{
    if(auto torchManager = _torchManager.lock()) {
        torchManager->SetStatus(status);
    }
}

void CRecognitionCore::RecognizeNumber()
{
    cv::Rect boundingRect;
    
    if(auto frameStorage = _frameStorage.lock()) {
        Mat frame;
        frameStorage->GetCurrentFrame(frame);
        if(auto torchManager = _torchManager.lock()) {
            torchManager->IncrementCounter();
        }
    }
}

cv::Mat CRecognitionCore::CaptureView()
{
    Mat normalizedMat;
    
    if(auto frameStorage = _frameStorage.lock()) {
        if(auto edgesDetector = _edgesDetector.lock()) {
            const size_t bufferLen = 1920 * 1080 * 3 / 2; //720 * 1280 * 3 / 2 ;
            uint8_t *uPlane, *vPlane;
            void* imgBuffer = malloc(bufferLen);
            
            memcpy(imgBuffer, frameStorage->GetYMat(), _bufferSizeY);
            
            const size_t planeWidth = 360;
            const size_t planeHeight = 640;
            uint8_t *planeBaseAddress = (uint8_t *)frameStorage->GetUVMat();
            size_t planeSize = planeWidth * planeHeight;
#ifdef __APPLE__
            // Convert Y'UV420sp to Y'UV420p
            uPlane = (uint8_t *)malloc(planeSize);
            vPlane = (uint8_t *)malloc(planeSize);
            
            for (uint32_t i = 0; i < (planeWidth * planeHeight / 4); i++) {
                uint8_t *uvSrc = &planeBaseAddress[i * 8];
                uint8_t *uDest = &uPlane[i * 4];
                uint8_t *vDest = &vPlane[i * 4];
                uint8x8x2_t loaded = vld2_u8(uvSrc);
                vst1_u8(uDest, loaded.val[0]);
                vst1_u8(vDest, loaded.val[1]);
            }
#else
            // Convert YV12 to Y'UV420p
            uPlane = planeBaseAddress;
            vPlane = &planeBaseAddress[planeSize];
#endif
            
            memcpy(((unsigned char *)imgBuffer) + _bufferSizeY, uPlane, planeSize);
            memcpy(((unsigned char *)imgBuffer) + _bufferSizeY + planeSize, vPlane, planeSize);
            
            Mat yuv = Mat(1280 + 1280/2, 720, CV_8UC1, imgBuffer);
            
            Mat rgb;
            cvtColor(yuv, rgb, CV_YUV2RGB_I420);
            
            
            Mat refinedMat = rgb(edgesDetector->GetInternalWindowRect());
            
            vector<ParametricLine> lines = frameStorage->GetEdges();
            frameStorage->NormalizeMatrix(refinedMat, lines, normalizedMat);
            
#ifdef __APPLE__
            free(uPlane);
            free(vPlane);
#endif
            free(imgBuffer);
            
            CardsOrientation orientation = frameStorage->GetYUVOrientation();
            
            if (orientation != CardsOrientationUpsideDown &&
                orientation != CardsOrientationPortrait) {
                
                CUtils::RotateMatrix90n(normalizedMat, normalizedMat, 90);
            }
            
            cv::resize(normalizedMat, normalizedMat, cv::Size(660,416));
        }
    }
    
    return normalizedMat;
}
