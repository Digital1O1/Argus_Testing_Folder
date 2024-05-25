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

#include "Error.h"
#include "Thread.h"

#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>

#include <NvEglRenderer.h>
#include <NvJpegEncoder.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <iostream>
#include <fstream>

#include <opencv2/opencv.hpp>
#include <cstdlib>

using namespace Argus;
using namespace EGLStream;
/* Configurations below can be overrided by cmdline */
static uint32_t CAPTURE_TIME = 1; /* In seconds. */
static int CAPTURE_FPS = 30;
static uint32_t SENSOR_MODE = 0;
static Size2D<uint32_t> PREVIEW_SIZE(640, 480);
static Size2D<uint32_t> CAPTURE_SIZE(1920, 1080);
static bool DO_STAT = false;
static bool VERBOSE_ENABLE = false;
static bool DO_JPEG_ENCODE = true;

#define JPEG_BUFFER_SIZE (CAPTURE_SIZE.area() * 3 / 2)

/* Debug print macros. */
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)

namespace ArgusSamples
{

    /*******************************************************************************
     * Base Consumer thread:
     *   Creates an EGLStream::FrameConsumer object to read frames from the
     *   OutputStream, then creates/populates an NvBuffer (dmabuf) from the frames
     *   to be processed by processV4L2Fd.
     ******************************************************************************/

    // ConsumerThread --> derived class
    // public Threads --> parent class
    class ConsumerThread : public Thread
    {
    public:
        explicit ConsumerThread(OutputStream *stream) : m_stream(stream),
                                                        m_dmabuf(-1)
        {
        }
        virtual ~ConsumerThread();

    protected:
        /** @name Thread methods */
        /**@{*/
        virtual bool threadInitialize();
        virtual bool threadExecute();
        virtual bool threadShutdown();
        /**@}*/

        virtual bool processV4L2Fd(int32_t fd, uint64_t frameNumber) = 0;

        OutputStream *m_stream;
        UniqueObj<FrameConsumer> m_consumer;
        int m_dmabuf;
        cv::Mat opencvFrame;
    };

    ConsumerThread::~ConsumerThread()
    {
        if (m_dmabuf != -1)
            NvBufferDestroy(m_dmabuf);
    }

    bool ConsumerThread::threadInitialize()
    {
        /* Create the FrameConsumer. */
        m_consumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_stream));
        if (!m_consumer)
            ORIGINATE_ERROR("Failed to create FrameConsumer");

        return true;
    }

    bool ConsumerThread::threadExecute()
    {
        IEGLOutputStream *iEglOutputStream = interface_cast<IEGLOutputStream>(m_stream);
        IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(m_consumer);

        /* Wait until the producer has connected to the stream. */
        CONSUMER_PRINT("Waiting until producer is connected...\n");
        if (iEglOutputStream->waitUntilConnected() != STATUS_OK)
            ORIGINATE_ERROR("Stream failed to connect.");
        CONSUMER_PRINT("Producer has connected; continuing.\n");

        while (true)
        {
            /* Acquire a frame. */
            UniqueObj<Frame> frame(iFrameConsumer->acquireFrame());
            IFrame *iFrame = interface_cast<IFrame>(frame);
            if (!iFrame)
                break;

            /* Get the image data from iFrame */
            EGLStream::Image *image = iFrame->getImage();
            if (!image)
                ORIGINATE_ERROR("Failed to get image from frame");

            /* Get image properties */

            // Size2D<uint32_t> size = image->getResolution();
            // uint32_t width = size.width();
            // uint32_t height = size.height();
            // const uint8_t *imageData = static_cast<const uint8_t *>(image->map());
            // if (!imageData)
            //     ORIGINATE_ERROR("Failed to map image data");

            // /* Create a cv::Mat object using the image data */
            // cv::Mat opencvFrame(height * 3 / 2, width, CV_8UC1, (void *)imageData);

            /* Get the IImageNativeBuffer extension interface. */
            NV::IImageNativeBuffer *iNativeBuffer =
                interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
            if (!iNativeBuffer)
                ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");

            /* If we don't already have a buffer, create one from this image.
               Otherwise, just blit to our buffer. */
            if (m_dmabuf == -1)
            {
                // m_dmabuf = iNativeBuffer->createNvBuffer(iEglOutputStream->getResolution(),
                //                                          NvBufferColorFormat_YUV420,
                //                                          NvBufferLayout_BlockLinear);
                m_dmabuf = iNativeBuffer->createNvBuffer(iEglOutputStream->getResolution(),
                                                         NvBufferColorFormat_ABGR32, NvBufferLayout_Pitch);
                if (m_dmabuf == -1)
                    CONSUMER_PRINT("\tFailed to create NvBuffer\n");
            }
            else if (iNativeBuffer->copyToNvBuffer(m_dmabuf) != STATUS_OK)
            {
                ORIGINATE_ERROR("Failed to copy frame to NvBuffer.");
            }

            /* Process frame. */
            processV4L2Fd(m_dmabuf, iFrame->getNumber());
        }

        CONSUMER_PRINT("Done.\n");

        requestShutdown();

        return true;
    }

    bool ConsumerThread::threadShutdown()
    {
        return true;
    }

    /*******************************************************************************
     * Preview Consumer thread:
     *   Read frames from the OutputStream and render it on display.
     ******************************************************************************/
    class PreviewConsumerThread : public ConsumerThread
    {
    public:
        // PreviewConsumerThread(OutputStream *stream, NvEglRenderer *renderer);
        PreviewConsumerThread(OutputStream *stream);
        ~PreviewConsumerThread();

    private:
        bool threadInitialize();
        bool threadShutdown();
        bool processV4L2Fd(int32_t fd, uint64_t frameNumber);

        // NvEglRenderer *m_renderer;
    };

    // PreviewConsumerThread::PreviewConsumerThread(OutputStream *stream,
    //                                              NvEglRenderer *renderer) : ConsumerThread(stream),
    //                                                                         m_renderer(renderer)
    PreviewConsumerThread::PreviewConsumerThread(OutputStream *stream) : ConsumerThread(stream)
    {
    }

    PreviewConsumerThread::~PreviewConsumerThread()
    {
    }

    bool PreviewConsumerThread::threadInitialize()
    {
        if (!ConsumerThread::threadInitialize())
            return false;

        // if (DO_STAT)
        //     m_renderer->enableProfiling();

        return true;
    }

    bool PreviewConsumerThread::threadShutdown()
    {
        // if (DO_STAT)
        //     m_renderer->printProfilingStats();

        return ConsumerThread::threadShutdown();
    }

    bool PreviewConsumerThread::processV4L2Fd(int32_t fd, uint64_t frameNumber)
    {
        // m_renderer->render(fd);
        void *pdata = NULL;
        NvBufferMemMap(fd, 0, NvBufferMem_Read, &pdata);
        NvBufferMemSyncForCpu(fd, 0, &pdata);
        cv::Mat imgbuf = cv::Mat(PREVIEW_SIZE.height(),
                                 PREVIEW_SIZE.width(),
                                 CV_8UC4, pdata);
        cv::Mat display_img;
        cvtColor(imgbuf, display_img, cv::COLOR_RGB2BGR);
        // cvtColor(imgbuf, display_img, CV_RGB);
        NvBufferMemUnMap(fd, 0, &pdata);
        cv::imshow("img", display_img);
        cv::waitKey(1);
        return true;
    }

    /*******************************************************************************
     * Capture Consumer thread:
     *   Read frames from the OutputStream and save it to JPEG file.
     ******************************************************************************/
    class CaptureConsumerThread : public ConsumerThread
    {
    public:
        CaptureConsumerThread(OutputStream *stream);
        ~CaptureConsumerThread();

    private:
        bool threadInitialize();
        bool threadShutdown();
        bool processV4L2Fd(int32_t fd, uint64_t frameNumber);

        NvJPEGEncoder *m_JpegEncoder;
        unsigned char *m_OutputBuffer;
    };

    CaptureConsumerThread::CaptureConsumerThread(OutputStream *stream) : ConsumerThread(stream),
                                                                         m_JpegEncoder(NULL),
                                                                         m_OutputBuffer(NULL)
    {
    }

    CaptureConsumerThread::~CaptureConsumerThread()
    {
        if (m_JpegEncoder)
            delete m_JpegEncoder;

        if (m_OutputBuffer)
            delete[] m_OutputBuffer;
    }

    bool CaptureConsumerThread::threadInitialize()
    {
        if (!ConsumerThread::threadInitialize())
            return false;

        m_OutputBuffer = new unsigned char[JPEG_BUFFER_SIZE];
        if (!m_OutputBuffer)
            return false;

        m_JpegEncoder = NvJPEGEncoder::createJPEGEncoder("jpenenc");
        if (!m_JpegEncoder)
            ORIGINATE_ERROR("Failed to create JPEGEncoder.");

        if (DO_STAT)
            m_JpegEncoder->enableProfiling();

        return true;
    }

    bool CaptureConsumerThread::threadShutdown()
    {
        if (DO_STAT)
            m_JpegEncoder->printProfilingStats();

        return ConsumerThread::threadShutdown();
    }

    bool CaptureConsumerThread::processV4L2Fd(int32_t fd, uint64_t frameNumber)
    {
        char filename[FILENAME_MAX];
        sprintf(filename, "output%03u.jpg", (unsigned)frameNumber);

        std::ofstream *outputFile = new std::ofstream(filename);
        if (outputFile)
        {
            unsigned long size = JPEG_BUFFER_SIZE;
            unsigned char *buffer = m_OutputBuffer;
            m_JpegEncoder->encodeFromFd(fd, JCS_YCbCr, &buffer, size);
            outputFile->write((char *)buffer, size);
            delete outputFile;
        }

        return true;
    }

    /**
     * Argus Producer thread:
     *   Opens the Argus camera driver, creates two OutputStreams to output to
     *   Preview Consumer and Capture Consumer respectively, then performs repeating
     *   capture requests for CAPTURE_TIME seconds before closing the producer and
     *   Argus driver.
     *
     * @param renderer     : render handler for camera preview
     */
    // static bool execute(NvEglRenderer *renderer)
    static bool execute()
    {
        /*
            class Argus::OutputStream
                - Object representing an output stream capable of receiving image frames from a capture.
                - OutputStream objects are used as the destination for image frames output from capture requests.
                - The operation of a stream, the source for its buffers, and the interfaces it supports depend on the StreamType of the stream.

            Argus::UniqueObj< T > Class Template Reference
                - Moveable smart pointer
                    - Mimicks std::unique_ptr
        */
        UniqueObj<OutputStream> captureStream;

        // Set to NULL to ensure well defined value
        CaptureConsumerThread *captureConsumerThread = NULL;

        /*
            Create the CameraProvider object and get the core interface

            class Argus::CameraProvider
                - Object providing the entry point to the libargus runtime.
                - It provides methods for querying the cameras in the system and for creating camera devices.

            static Argus::CameraProvider *Argus::CameraProvider::create(Argus::Status *status = (Argus::Status *)__null)
                - Creates and returns a new CameraProvider.
                - If a CameraProvider object has already been created, this method will return a pointer to that object.
                - Parameters:
                    status – Optional pointer to return success/status of the call.

            UniqueObj<CameraProvider>
                - Smart pointer
                - Takes ownership of 'CameraProvider' object that's returned by create()
        */

        // Smart pointer takes ownership of CameraProvider object
        UniqueObj<CameraProvider> cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());

        // If successful, returns pointer to 'ICameraProvider' interface
        // If not, returns nullptr
        /*
            interface_cast<>
                - Similar to dynamic_cast
                - Performs downcoasting of pointeres/reference to objects in heritance hierarchies
                - Designed to be used in polymorphic classes that have at least one virtual function
                - If conversion not safe
                    - It'll return a nullptr
                    - Or throw a std::bad_cast
        */
        ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
        if (!iCameraProvider)
            ORIGINATE_ERROR("Failed to create CameraProvider");

        /* Get the camera devices */
        std::vector<CameraDevice *> cameraDevices;
        /*
            getCameraDevices
                - virtual Argus::Status Argus::ICameraProvider::getCameraDevices(std::vector<Argus::CameraDevice *> *devices) const
                - Return list of camera devices exposed by provider
                - Includes devices already in use by active CaptureSession
                - Application responsibility
                    - Check device availability
                    - Handle errors returned when CaptureSession creation fails due to device already being used
        */
        // Pointer to ICameraProvider
        iCameraProvider->getCameraDevices(&cameraDevices);
        if (cameraDevices.size() == 0)
            ORIGINATE_ERROR("No cameras available");
        else
        {
            std::cout << "Number of cameras : " << cameraDevices.size() << std::endl;
        }

        ICameraProperties *iCameraProperties = interface_cast<ICameraProperties>(cameraDevices[0]);
        if (!iCameraProperties)
            ORIGINATE_ERROR("Failed to get ICameraProperties interface");

        /* Create the capture session using the first device and get the core interface */

        /*
            class Argus::CaptureSession
                - Object that controls all operations on a single sensor.
                - A capture session is bound to a single sensor (or, in future, a group of synchronized sensors)
                - Provides methods to perform captures on that sensor (via the ICaptureSession interface).

            virtual Argus::CaptureSession *Argus::ICameraProvider::createCaptureSession(Argus::CameraDevice *device, Argus::Status *status = (Argus::Status *)__null)
                - Creates and returns a new CaptureSession using the given device.
                - STATUS_UNAVAILABLE will be placed into status if the device is already in use.
                - Parameters:
                    - device – The device to use for the CaptureSession.
                    - status – Optional pointer to return success/status of the call.
                - Returns:
                    - The new CaptureSession
                    - Or NULL if an error occurred.
        */
        UniqueObj<CaptureSession> captureSession(iCameraProvider->createCaptureSession(cameraDevices[0]));
        ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
        if (!iCaptureSession)
            ORIGINATE_ERROR("Failed to get ICaptureSession interface");

        /* Initiaialize the settings of output stream */
        PRODUCER_PRINT("Creating output stream\n");

        /*
            Initiaialize the settings of output stream

            UniqueObj<OutputStreamSettings> --> class Argus::OutputStreamSettings
                - Container for settings used to configure/create an OutputStream.
                - The interfaces and configuration supported by these settings objects depend on the StreamType that was provided during settings creation (see ICaptureSession::createOutputStreamSettings).
                - These objects are passed to ICaptureSession::createOutputStream to create OutputStream objects, after which they may be destroyed.

            virtual Argus::OutputStreamSettings *Argus::ICaptureSession::createOutputStreamSettings(const Argus::StreamType &type, Argus::Status *status = (Argus::Status *)__null)
                - Creates an OutputStreamSettings object that is used to configure the creation of an OutputStream (see createOutputStream).
                - The type of OutputStream that will be configured and created by these settings are determined by the StreamType.
                - Parameters:
                    - type – The type of the OutputStream to configure/create with these settings.
                    - status – An optional pointer to return success/status.
                - Returns:
                    - The newly created OutputStreamSettings, or NULL on failure.

            IEGLOutputStreamSettings
                - Interface that exposes settings used for EGLStream-linked OutputStream creation
        */
        UniqueObj<OutputStreamSettings> streamSettings(iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
        IEGLOutputStreamSettings *iEglStreamSettings = interface_cast<IEGLOutputStreamSettings>(streamSettings);
        if (!iEglStreamSettings)
            ORIGINATE_ERROR("Failed to get IEGLOutputStreamSettings interface");

        iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
        // iEglStreamSettings->setEGLDisplay(renderer->getEGLDisplay());
        iEglStreamSettings->setResolution(PREVIEW_SIZE);

        /*
            Based on above streamSettings, create the preview stream, and capture stream if JPEG Encode is required

            class Argus::OutputStream
                - Object representing an output stream capable of receiving image frames from a capture.
                - OutputStream objects are used as the destination for image frames output from capture requests.
                    - In this case it should be previewStream as the object
                - The operation of a stream, the source for its buffers, and the interfaces it supports depend on the StreamType of the stream.


            virtual Argus::OutputStream *Argus::ICaptureSession::createOutputStream(const Argus::OutputStreamSettings *settings, Argus::Status *status = (Argus::Status *)__null)
                - Creates an OutputStream object using the settings configured by an OutputStreamSettings object (see createOutputStreamSettings).
                - Parameters:
                    - settings – The settings to use for the new output stream.
                    - status – An optional pointer to return success/status.
                - Returns:
                    - The newly created OutputStream, or NULL on failure.

        */
        UniqueObj<OutputStream> previewStream(iCaptureSession->createOutputStream(streamSettings.get()));
        if (DO_JPEG_ENCODE)
        {
            // static Size2D<uint32_t> CAPTURE_SIZE(1920, 1080);
            iEglStreamSettings->setResolution(CAPTURE_SIZE);

            /*
                class Argus::OutputStream
                    - Object representing an output stream capable of receiving image frames from a capture.
                    - OutputStream objects are used as the destination for image frames output from capture requests.
                    - The operation of a stream, the source for its buffers, and the interfaces it supports depend on the StreamType of the stream.
            */
            captureStream = (UniqueObj<OutputStream>)iCaptureSession->createOutputStream(streamSettings.get());
        }

        /*
            Launch the FrameConsumer thread to consume frames from the OutputStream

            NvEglRenderer *renderer
                - Argus Producer thread: Opens the Argus camera driver,
                    - creates two OutputStreams to output to Preview Consumer and Capture Consumer respectively,
                    - Then performs repeating capture requests for CAPTURE_TIME seconds before closing the producer and Argus driver.
                - Parameters:
                    - renderer – : render handler for camera preview

            PreviewConsumerThread
                - Read frames from the OutputStream and render it on display.

        */
        PRODUCER_PRINT("Launching consumer thread\n");
        // PreviewConsumerThread previewConsumerThread(previewStream.get(), renderer);
        PreviewConsumerThread previewConsumerThread(previewStream.get());

        PROPAGATE_ERROR(previewConsumerThread.initialize());
        if (DO_JPEG_ENCODE)
        {
            captureConsumerThread = new ArgusSamples::CaptureConsumerThread(captureStream.get());
            PROPAGATE_ERROR(captureConsumerThread->initialize());
        }

        /* Wait until the consumer thread is connected to the stream */
        PROPAGATE_ERROR(previewConsumerThread.waitRunning());
        if (DO_JPEG_ENCODE)
            PROPAGATE_ERROR(captureConsumerThread->waitRunning());

        /* Create capture request and enable its output stream */
        UniqueObj<Request> request(iCaptureSession->createRequest());
        IRequest *iRequest = interface_cast<IRequest>(request);
        if (!iRequest)
            ORIGINATE_ERROR("Failed to create Request");
        iRequest->enableOutputStream(previewStream.get());
        if (DO_JPEG_ENCODE)
            iRequest->enableOutputStream(captureStream.get());

        ISensorMode *iSensorMode;
        std::vector<SensorMode *> sensorModes;
        iCameraProperties->getBasicSensorModes(&sensorModes);
        if (sensorModes.size() == 0)
            ORIGINATE_ERROR("Failed to get sensor modes");

        PRODUCER_PRINT("Available Sensor modes :\n");
        for (uint32_t i = 0; i < sensorModes.size(); i++)
        {
            iSensorMode = interface_cast<ISensorMode>(sensorModes[i]);
            Size2D<uint32_t> resolution = iSensorMode->getResolution();
            PRODUCER_PRINT("[%u] W=%u H=%u\n", i, resolution.width(), resolution.height());
        }

        ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
        if (!iSourceSettings)
            ORIGINATE_ERROR("Failed to get ISourceSettings interface");

        /* Check and set sensor mode */
        if (SENSOR_MODE >= sensorModes.size())
            ORIGINATE_ERROR("Sensor mode index is out of range");
        SensorMode *sensorMode = sensorModes[SENSOR_MODE];
        iSensorMode = interface_cast<ISensorMode>(sensorMode);
        iSourceSettings->setSensorMode(sensorMode);

        /* Check fps */
        Range<uint64_t> sensorDuration(iSensorMode->getFrameDurationRange());
        Range<uint64_t> desireDuration(1e9 / CAPTURE_FPS + 0.9);
        if (desireDuration.min() < sensorDuration.min() ||
            desireDuration.max() > sensorDuration.max())
        {
            PRODUCER_PRINT("Requested FPS out of range. Fall back to 30\n");
            CAPTURE_FPS = 30;
        }
        /* Set the fps */
        iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9 / CAPTURE_FPS));
        // renderer->setFPS((float)CAPTURE_FPS);

        /* Submit capture requests. */
        PRODUCER_PRINT("Starting repeat capture requests.\n");
        if (iCaptureSession->repeat(request.get()) != STATUS_OK)
            ORIGINATE_ERROR("Failed to start repeat capture request");

        /* Wait for CAPTURE_TIME seconds. */
        sleep(CAPTURE_TIME);

        /* Stop the repeating request and wait for idle. */
        iCaptureSession->stopRepeat();
        iCaptureSession->waitForIdle();

        /* Destroy the output stream to end the consumer thread. */
        previewStream.reset();
        if (DO_JPEG_ENCODE)
            captureStream.reset();

        /* Wait for the consumer thread to complete. */
        PROPAGATE_ERROR(previewConsumerThread.shutdown());
        if (DO_JPEG_ENCODE)
        {
            PROPAGATE_ERROR(captureConsumerThread->shutdown());
            delete captureConsumerThread;
        }

        PRODUCER_PRINT("Done -- exiting.\n");

        return true;
    }

}; /* namespace ArgusSamples */

int main()
{
    if (setenv("DISPLAY", ":0", 1) != 0)
    {
        std::cerr << "COULDN'T SET DISPLAY VARIABLE TO 0 EXITING PROGRAM NOW..." << std::endl;
        return 1;
    }
    else
    {
        printf("Program started\r\n");
    }

    // Make sure to set 'export DISPLAY=:0' or you'll throw a fault and not be able to run the program
    // NvEglRenderer *renderer = NvEglRenderer::createEglRenderer("renderer0", PREVIEW_SIZE.width(),
    //                                                            PREVIEW_SIZE.height(), 0, 0);
    // if (!renderer)
    //     ORIGINATE_ERROR("Failed to create EGLRenderer.");
    // else
    //     printf("EGLRenderer created successful\r\n");

    // Create object representing output stream to receive image frames
    UniqueObj<OutputStream> captureStream;
    ArgusSamples::CaptureConsumerThread *captureConsumerThread = NULL;

    // Create CameraProvider Object to get core interface to control camera
    // UniqueObj == Smart pointer
    // Don't place breakpoint before this or the debugger will freeze up
    UniqueObj<CameraProvider> cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());

    // Use interface_cast<> on cameraProvider to get/use pointer with 'ICameraProvider' interface
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to create CameraProvider");

    // Create vector to store the numbers the jetson detects
    std::vector<CameraDevice *> cameraDevices;

    //
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No cameras available");
    else
    {
        std::cout << "Number of cameras : " << cameraDevices.size() << std::endl;
    }

    // Use interface_cast<> again on cameraDevice[0] to get/use pointer with ICameraProperties
    ICameraProperties *iCameraProperties = interface_cast<ICameraProperties>(cameraDevices[0]);
    if (!iCameraProperties)
        ORIGINATE_ERROR("Failed to get ICameraProperties interface");

    // Create CaptureSession with first camera device and get core interface

    UniqueObj<CaptureSession> captureSession(iCameraProvider->createCaptureSession(cameraDevices[0]));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
    if (!iCaptureSession)
        ORIGINATE_ERROR("Failed to get ICaptureSession interface");

    /* Initiaialize the settings of output stream */
    PRODUCER_PRINT("Creating output stream\n");

    // Initiaialize the settings of output stream
    UniqueObj<OutputStreamSettings> streamSettings(iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEglStreamSettings = interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iEglStreamSettings)
        ORIGINATE_ERROR("Failed to get IEGLOutputStreamSettings interface");

    iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    // iEglStreamSettings->setEGLDisplay(renderer->getEGLDisplay());
    iEglStreamSettings->setResolution(PREVIEW_SIZE);

    // Based on above streamSettings, create the preview stream, and capture stream if JPEG Encode is required
    UniqueObj<OutputStream> previewStream(iCaptureSession->createOutputStream(streamSettings.get()));
    if (DO_JPEG_ENCODE)
    {
        // static Size2D<uint32_t> CAPTURE_SIZE(1920, 1080);
        iEglStreamSettings->setResolution(CAPTURE_SIZE);

        /*
            class Argus::OutputStream
                - Object representing an output stream capable of receiving image frames from a capture.
                - OutputStream objects are used as the destination for image frames output from capture requests.
                - The operation of a stream, the source for its buffers, and the interfaces it supports depend on the StreamType of the stream.
        */
        captureStream = (UniqueObj<OutputStream>)iCaptureSession->createOutputStream(streamSettings.get());
    }

    PRODUCER_PRINT("Launching consumer thread\n");
    // ArgusSamples::previewConsumerThread(previewStream.get(), renderer);
    // Must declare namespace | The actual class | Then the object name
    // ArgusSamples::PreviewConsumerThread previewConsumerThread(previewStream.get(), renderer);
    ArgusSamples::PreviewConsumerThread previewConsumerThread(previewStream.get());

    PROPAGATE_ERROR(previewConsumerThread.initialize());
    if (DO_JPEG_ENCODE)
    {
        captureConsumerThread = new ArgusSamples::CaptureConsumerThread(captureStream.get());
        PROPAGATE_ERROR(captureConsumerThread->initialize());
    }

    /* Wait until the consumer thread is connected to the stream */
    PROPAGATE_ERROR(previewConsumerThread.waitRunning());
    if (DO_JPEG_ENCODE)
        PROPAGATE_ERROR(captureConsumerThread->waitRunning());

    /* Create capture request and enable its output stream */
    UniqueObj<Request> request(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(previewStream.get());
    if (DO_JPEG_ENCODE)
        iRequest->enableOutputStream(captureStream.get());

    ISensorMode *iSensorMode;
    std::vector<SensorMode *> sensorModes;
    iCameraProperties->getBasicSensorModes(&sensorModes);
    if (sensorModes.size() == 0)
        ORIGINATE_ERROR("Failed to get sensor modes");

    PRODUCER_PRINT("Available Sensor modes :\n");
    for (uint32_t i = 0; i < sensorModes.size(); i++)
    {
        iSensorMode = interface_cast<ISensorMode>(sensorModes[i]);
        Size2D<uint32_t> resolution = iSensorMode->getResolution();
        PRODUCER_PRINT("[%u] W=%u H=%u\n", i, resolution.width(), resolution.height());
    }

    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get ISourceSettings interface");

    /* Check and set sensor mode */
    if (SENSOR_MODE >= sensorModes.size())
        ORIGINATE_ERROR("Sensor mode index is out of range");
    SensorMode *sensorMode = sensorModes[SENSOR_MODE];
    iSensorMode = interface_cast<ISensorMode>(sensorMode);
    iSourceSettings->setSensorMode(sensorMode);

    /* Check fps */
    Range<uint64_t> sensorDuration(iSensorMode->getFrameDurationRange());
    Range<uint64_t> desireDuration(1e9 / CAPTURE_FPS + 0.9);
    if (desireDuration.min() < sensorDuration.min() ||
        desireDuration.max() > sensorDuration.max())
    {
        PRODUCER_PRINT("Requested FPS out of range. Fall back to 30\n");
        CAPTURE_FPS = 30;
    }
    /* Set the fps */
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9 / CAPTURE_FPS));
    // renderer->setFPS((float)CAPTURE_FPS);

    /* Submit capture requests. */
    PRODUCER_PRINT("Starting repeat capture requests.\n");
    if (iCaptureSession->repeat(request.get()) != STATUS_OK)
        ORIGINATE_ERROR("Failed to start repeat capture request");

    /* Wait for CAPTURE_TIME seconds. */
    sleep(CAPTURE_TIME);

    /* Stop the repeating request and wait for idle. */
    iCaptureSession->stopRepeat();
    iCaptureSession->waitForIdle();

    /* Destroy the output stream to end the consumer thread. */
    previewStream.reset();
    if (DO_JPEG_ENCODE)
        captureStream.reset();

    /* Wait for the consumer thread to complete. */
    PROPAGATE_ERROR(previewConsumerThread.shutdown());
    if (DO_JPEG_ENCODE)
    {
        PROPAGATE_ERROR(captureConsumerThread->shutdown());
        delete captureConsumerThread;
    }

    return 0;
}