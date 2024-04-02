/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include "ArgusHelpers.h"
#include "CommonOptions.h"

// #define STATUS_OK 0
// #define STATUS_INVALID_PARAMS 1
// #define STATUS_INVALID_SETTINGS 2
// #define STATUS_UNAVAILABLE 3
// #define STATUS_OUT_OF_MEMORY 4
// #define STATUS_UNIMPLEMENTED 5
// #define STATUS_TIMEOUT 6
// #define STATUS_CANCELLED 7
// #define STATUS_DISCONNECTED 8
// #define STATUS_END_OF_STREAM 9

#define NANOSECOND_MULTIPLIER 1000000
#define TIME_DURATION 10
#define EXIT_IF_NULL(val, msg)   \
    {                            \
        if (!val)                \
        {                        \
            printf("%s\n", msg); \
            return EXIT_FAILURE; \
        }                        \
    }
#define EXIT_IF_NOT_OK(val, msg)     \
    {                                \
        if (val != Argus::STATUS_OK) \
        {                            \
            printf("%s\n", msg);     \
            return EXIT_FAILURE;     \
        }                            \
    }

#ifdef ANDROID
#define FILE_PREFIX "/sdcard/DCIM/"
#else
#define FILE_PREFIX ""
#endif

/*
 * Program: oneShot
 * Function: Capture a single image from a camera device and write to a JPG file
 * Purpose: To demonstrate the most simplistic approach to getting the Argus Framework
 *          running, submitting a capture request, retrieving the resulting image and
 *          then writing the image as a JPEG formatted file.
 */

int main(int argc, char **argv)
{
    // printf("test\r\n");
    ArgusSamples::CommonOptions options(basename(argv[0]),
                                        ArgusSamples::CommonOptions::Option_D_CameraDevice |
                                            ArgusSamples::CommonOptions::Option_M_SensorMode);
    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    const uint64_t aquireFrameDuration = TIME_DURATION * NANOSECOND_MULTIPLIER;

    /*
     * Set up Argus API Framework, identify available camera devices, and create
     * a capture session for the first available device
     */

    /*
        Argus::UniqueObj
            - Smart pointer
            - Mimicks the use of std::unique_ptr
            - Offers moveable smart pointer
                - Can call destory() on Destructable objects being reference by the pointer once it leaves the scope
            - interface_cast method is overloaded to take UniqueObj pointers
                - allows the following
                {
                    UniqueObj<Request> request(iSession->createRequest());
                    IRequest *iRequest = interface_cast<IRequest>(request);
                    /// Request is destroy()ed when leaving scope
                }

        cameraProvider is the smart pointer

        Argus::CameraProvider::create()
            - Syntax : static CameraProvider* Argus::CameraProvider::create	(	Status * 	status = NULL	)
            - Creates and returns new CameraProvider
            - If CameraProvider object already created, this method will return a pointer to that object
            - Parameters
                - status OPTIONAL pointer to return success/status of the call

        class Argus::CameraProvider
            - Object providing the entry point to the libargus runtime.
            - It provides methods for querying the cameras in the system and for creating camera devices
    */
    Argus::Status cameraProviderStatus;

    Argus::UniqueObj<Argus::CameraProvider> cameraProvider(Argus::CameraProvider::create(&cameraProviderStatus));

    // Assuming cameraProviderStatus is assigned a value somewhere before printing

    if (cameraProviderStatus == Argus::STATUS_OK)
    {
        printf("\n------------------------------------------Camera ready------------------------------------------\r\n");
    }

    /*
        ICameraProvider
            - Interface to the core CameraProvider methods
                - Argus::Interface Class Reference
                - Reference lnk : https://docs.nvidia.com/jetson/l4t-multimedia/classArgus_1_1Interface.html
                - Description
                    - By convention, every Interface subclass exposes a public static method called id()
                    - It returns a unique InterfaceID for that interface
                        - Which is needed for the interface_cast<> template to work for that interface
                    - Must check out reference link for interface chart : https://docs.nvidia.com/jetson/l4t-multimedia/classArgus_1_1Interface.html
            - CameraProvider method reference link : https://docs.nvidia.com/jetson/l4t-multimedia/classArgus_1_1CameraProvider.html

        Public Member Functions for ICameraProvider
            - virtual const std::string & 	getVersion () const =0
               - Returns the version number of the libargus implementation. More...

            - virtual const std::string & 	getVendor () const =0
               - Returns the vendor string for the libargus implementation. More...

            - virtual bool 	supportsExtension (const ExtensionName &extension) const =0
               - Returns whether or not an extension is supported by this libargus implementation. More...
               - Should return success/status of object
                 - Refernece link to Status ENUMS : https://docs.nvidia.com/jetson/l4t-multimedia/namespaceArgus.html#a43dee5758547aaf78710c7c1fe122fe3

            - virtual Status 	getCameraDevices (std::vector< CameraDevice * > *devices) const =0
               - Returns the list of camera devices that are exposed by the provider. More...

            - virtual CaptureSession * 	createCaptureSession (CameraDevice *device, Status *status=NULL)=0
               - Creates and returns a new CaptureSession using the given device. More...

            - virtual CaptureSession * 	createCaptureSession (const std::vector< CameraDevice * > &devices, Status *status=NULL)=0
               - Creates and returns a new CaptureSession using the given device(s). More...
    */
    Argus::ICameraProvider *iCameraProvider =
        Argus::interface_cast<Argus::ICameraProvider>(cameraProvider);
    EXIT_IF_NULL(iCameraProvider, "Cannot get core camera provider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());
    std::cout << iCameraProvider->id;

    // virtual Argus::Status Argus::ICameraProvider::getCameraDevices(std::vector<Argus::CameraDevice *> *devices) const
    std::vector<Argus::CameraDevice *> *devices_;
    std::cout << "iCameraProvider->getCameraDevices : " << iCameraProvider->getCameraDevices(devices_) << std::endl;
    // --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    // Argus::CameraDevice *ArgusSamples::ArgusHelpers::getCameraDevice(Argus::CameraProvider *cameraProvider, uint32_t cameraDeviceIndex)
    // Object representing a single camera device. CameraDevices are provided by a CameraProvider and are used to access the camera devices available within the system. Each device is based on a single sensor or a set of synchronized sensors.

    /*
        CameraDevice
            - Reference link : https://docs.nvidia.com/jetson/l4t-multimedia/group__ArgusCameraDevice.html
            - Object representing single camera device
            - CameraDevices provided by 'CameraProvider'
                - Used to access camera devices available within system
                - Each device based on
                    - Single sensor
                    - Set of synchronized sensors
    */
    Argus::CameraDevice *device = ArgusSamples::ArgusHelpers::getCameraDevice(
        cameraProvider.get(), options.cameraDeviceIndex());

    Argus::ICameraProperties *iCameraProperties =
        Argus::interface_cast<Argus::ICameraProperties>(device);
    if (!iCameraProperties)
    {
        REPORT_ERROR("Failed to get ICameraProperties interface");
        return EXIT_FAILURE;
    }
    else
    {
        std::cout << "cameraProvider.get() : " << cameraProvider.get() << std::endl;
        std::cout << "options.cameraDeviceIndex() : " << options.cameraDeviceIndex() << std::endl;
    }

    // Argus::SensorMode *sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
    //     device, options.sensorModeIndex());

    const uint32_t sensorModeIndex = 1; // Change this to the desired sensor mode index

    Argus::SensorMode *sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
        device, sensorModeIndex);

    // Argus::SensorMode *sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
    //     device, 1);

    printf("Sensor mode index: %u\n", options.sensorModeIndex());
    Argus::ISensorMode *iSensorMode =
        Argus::interface_cast<Argus::ISensorMode>(sensorMode);
    if (!iSensorMode)
    {
        REPORT_ERROR("Failed to get sensor mode interface");
        return EXIT_FAILURE;
    }

    printf("Capturing from device %d using sensor mode %d (%d x %d)\n",
           options.cameraDeviceIndex(), options.sensorModeIndex(),
           iSensorMode->getResolution().width(), iSensorMode->getResolution().height());

    Argus::Status status;
    Argus::UniqueObj<Argus::CaptureSession> captureSession(
        iCameraProvider->createCaptureSession(device, &status));
    EXIT_IF_NOT_OK(status, "Failed to create capture session");

    if (status == Argus::STATUS_OK)
        printf("Captured session created\r\n");

    Argus::ICaptureSession *iSession =
        Argus::interface_cast<Argus::ICaptureSession>(captureSession);
    EXIT_IF_NULL(iSession, "Cannot get Capture Session Interface");

    /*
     * Creates the stream between the Argus camera image capturing
     * sub-system (producer) and the image acquisition code (consumer).  A consumer object is
     * created from the stream to be used to request the image frame.  A successfully submitted
     * capture request activates the stream's functionality to eventually make a frame available
     * for acquisition.
     */

    Argus::UniqueObj<Argus::OutputStreamSettings> streamSettings(
        iSession->createOutputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLOutputStreamSettings *iEGLStreamSettings =
        Argus::interface_cast<Argus::IEGLOutputStreamSettings>(streamSettings);
    EXIT_IF_NULL(iEGLStreamSettings, "Cannot get IEGLOutputStreamSettings Interface");
    iEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
    iEGLStreamSettings->setResolution(iSensorMode->getResolution());
    iEGLStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> stream(
        iSession->createOutputStream(streamSettings.get()));
    EXIT_IF_NULL(stream, "Failed to create EGLOutputStream");

    Argus::UniqueObj<EGLStream::FrameConsumer> consumer(
        EGLStream::FrameConsumer::create(stream.get()));

    EGLStream::IFrameConsumer *iFrameConsumer =
        Argus::interface_cast<EGLStream::IFrameConsumer>(consumer);
    EXIT_IF_NULL(iFrameConsumer, "Failed to initialize Consumer");

    Argus::UniqueObj<Argus::Request> request(
        iSession->createRequest(Argus::CAPTURE_INTENT_STILL_CAPTURE));

    Argus::IRequest *iRequest = Argus::interface_cast<Argus::IRequest>(request);
    EXIT_IF_NULL(iRequest, "Failed to get capture request interface");

    status = iRequest->enableOutputStream(stream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable stream in capture request");

    Argus::ISourceSettings *iSourceSettings =
        Argus::interface_cast<Argus::ISourceSettings>(request);
    EXIT_IF_NULL(iSourceSettings, "Failed to get source settings request interface");
    iSourceSettings->setSensorMode(sensorMode);

    uint32_t requestId = iSession->capture(request.get());
    EXIT_IF_NULL(requestId, "Failed to submit capture request");

    /*
     * Acquire a frame generated by the capture request, get the image from the frame
     * and create a .JPG file of the captured image
     */
    Argus::UniqueObj<EGLStream::Frame> frame(
        iFrameConsumer->acquireFrame(aquireFrameDuration, &status));

    EGLStream::IFrame *iFrame = Argus::interface_cast<EGLStream::IFrame>(frame);
    EXIT_IF_NULL(iFrame, "Failed to get IFrame interface");

    EGLStream::Image *image = iFrame->getImage();
    EXIT_IF_NULL(image, "Failed to get Image from iFrame->getImage()");

    EGLStream::IImageJPEG *iImageJPEG = Argus::interface_cast<EGLStream::IImageJPEG>(image);
    EXIT_IF_NULL(iImageJPEG, "Failed to get ImageJPEG Interface");

    status = iImageJPEG->writeJPEG(FILE_PREFIX "argus_oneShot.jpg");
    EXIT_IF_NOT_OK(status, "Failed to write JPEG");

    printf("Wrote file: " FILE_PREFIX "argus_oneShot.jpg\n");

    // Shut down Argus.
    cameraProvider.reset();

    return EXIT_SUCCESS;
}
