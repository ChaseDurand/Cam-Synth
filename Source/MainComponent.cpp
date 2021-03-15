/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "../JuceLibraryCode/JuceHeader.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>


class Visualiser : public juce::AudioVisualiserComponent
{
public:
    Visualiser() : AudioVisualiserComponent (2)
    {
        setBufferSize(256);
        setSamplesPerBlock(8);
        setColours(juce::Colours::black, juce::Colours::deepskyblue);
    }
};


//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainContentComponent   : public juce::AudioAppComponent,
                               public juce::Slider::Listener,
                               public juce::Timer
{
public:
    
    

    
    void sliderValueChanged (juce::Slider* slider) override
    {
        if (slider == &freqSlider)
        {
            currentFrequency = freqSlider.getValue();
            
        } else if (slider == &ampSlider)
        {
            amplitude = ampSlider.getValue();
        }
    }
    
    
    void updateFrequency(const double& bufferSize)
    {
        if (targetFrequency != currentFrequency)
        {
           
        const double frequencyIncrement = (targetFrequency - currentFrequency) / bufferSize;
        increment = (currentFrequency + frequencyIncrement) * wtSize / currentSampleRate;
            
        phase = fmod ((phase + increment), wtSize);
        } else
        {
            increment = currentFrequency * wtSize / currentSampleRate;
            phase = fmod ((phase + increment), wtSize);
        }
    }

    //==============================================================================
    MainContentComponent()
    {
        setSize (800, 800);
     
        freqSlider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
        freqSlider.setRange(30.0, 2000.0, 1.0);
        freqSlider.setTextValueSuffix("Hz");
        freqSlider.addListener(this);
        freqSlider.setValue(440.0);
        addAndMakeVisible(freqSlider);
        freqLabel.setText("Frequency", juce::dontSendNotification);
        freqLabel.attachToComponent(&freqSlider, true);
       
        ampSlider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
        ampSlider.setRange(0.0, 1.0, 0.01);
        ampSlider.addListener(this);
        ampSlider.setValue(0.0);
        addAndMakeVisible(ampSlider);
        ampLabel.setText("Amplitude", juce::dontSendNotification);
        ampLabel.attachToComponent(&ampSlider, true);
        
        addAndMakeVisible(visualiser);
    
        // specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
        
        addAndMakeVisible (cameraSelectorComboBox);
        updateCameraList();
        cameraSelectorComboBox.setSelectedId (1);
        cameraSelectorComboBox.onChange = [this] { cameraChanged(); };
        
        addAndMakeVisible (snapshotButton);
        snapshotButton.onClick = [this] { takeSnapshot(); };
        snapshotButton.setEnabled (false);
        
        addAndMakeVisible (lastSnapshot);
        
        cameraSelectorComboBox.setSelectedId (2);
    }

    ~MainContentComponent()
    {
        shutdownAudio();
    }
    
    void updateCameraList()
    {
        cameraSelectorComboBox.clear();
        cameraSelectorComboBox.addItem ("No camera", 1);
        cameraSelectorComboBox.addSeparator();
        
        auto cameras = juce::CameraDevice::getAvailableDevices();
        
        for (int i = 0; i < cameras.size(); ++i)
            cameraSelectorComboBox.addItem (cameras[i], i + 2);
    }

    void cameraChanged()
    {
        // This is called when the user chooses a camera from the drop-down list.
#if JUCE_IOS
        // On iOS, when switching camera, open the new camera first, so that it can
        // share the underlying camera session with the old camera. Otherwise, the
        // session would have to be closed first, which can take several seconds.
        if (cameraSelectorComboBox.getSelectedId() == 1)
            cameraDevice.reset();
#else
        cameraDevice.reset();
#endif
        cameraPreviewComp.reset();
        recordingMovie = false;
        
        if (cameraSelectorComboBox.getSelectedId() > 1)
        {
#if JUCE_ANDROID || JUCE_IOS
            openCameraAsync();
#else
            cameraDeviceOpenResult (juce::CameraDevice::openDevice (cameraSelectorComboBox.getSelectedId() - 2), {});
#endif
        }
        else
        {
            snapshotButton   .setEnabled (cameraDevice != nullptr && ! contentSharingPending);
            recordMovieButton.setEnabled (cameraDevice != nullptr && ! contentSharingPending);
            resized();
        }
    }
    
    void openCameraAsync()
    {
        SafePointer<MainContentComponent> safeThis (this);
        
        juce::CameraDevice::openDeviceAsync (cameraSelectorComboBox.getSelectedId() - 2,
                                             [safeThis] (juce::CameraDevice* device, const juce::String& error) mutable
                                             {
                                                 if (safeThis)
                                                     safeThis->cameraDeviceOpenResult (device, error);
                                             });
    }
    
    void cameraDeviceOpenResult (juce::CameraDevice* device, const juce::String& error)
    {
        // If camera opening worked, create a preview component for it..
        cameraDevice.reset (device);
        
        if (cameraDevice.get() != nullptr)
        {
#if JUCE_ANDROID
            SafePointer<MainContentComponent> safeThis (this);
            cameraDevice->onErrorOccurred = [safeThis] (const String& cameraError) mutable { if (safeThis) safeThis->errorOccurred (cameraError); };
#endif
            cameraPreviewComp.reset (cameraDevice->createViewerComponent());
            addAndMakeVisible (cameraPreviewComp.get());
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Camera open failed",
                                                    "Camera open failed, reason: " + error);
        }
        
        snapshotButton   .setEnabled (cameraDevice.get() != nullptr && ! contentSharingPending);
        recordMovieButton.setEnabled (cameraDevice.get() != nullptr && ! contentSharingPending);
        resized();
    }
    
    
    
    
    
    void startRecording()
    {
        if (cameraDevice.get() != nullptr)
        {
            // The user has clicked the record movie button..
            if (! recordingMovie)
            {
                // Start recording to a file on the user's desktop..
                recordingMovie = true;
                
#if JUCE_ANDROID || JUCE_IOS
                recordingFile = File::getSpecialLocation (File::tempDirectory)
#else
                recordingFile = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
#endif
                .getNonexistentChildFile ("JuceCameraVideoDemo", juce::CameraDevice::getFileExtension());
                
#if JUCE_ANDROID
                // Android does not support taking pictures while recording video.
                snapshotButton.setEnabled (false);
#endif
                
                cameraSelectorComboBox.setEnabled (false);
                cameraDevice->startRecordingToFile (recordingFile);
                recordMovieButton.setButtonText ("Stop Recording");
            }
            else
            {
                // Already recording, so stop...
                recordingMovie = false;
                cameraDevice->stopRecording();
#if ! JUCE_ANDROID && ! JUCE_IOS
                recordMovieButton.setButtonText ("Start recording (to a file on your desktop)");
#else
                recordMovieButton.setButtonText ("Record a movie");
#endif
                cameraSelectorComboBox.setEnabled (true);
                
#if JUCE_ANDROID
                snapshotButton.setEnabled (true);
#endif
                
#if JUCE_CONTENT_SHARING
                URL url (recordingFile);
                
                snapshotButton   .setEnabled (false);
                recordMovieButton.setEnabled (false);
                contentSharingPending = true;
                
                SafePointer<MainContentComponent> safeThis (this);
                
                juce::ContentSharer::getInstance()->shareFiles ({url},
                                                                [safeThis] (bool success, const String&) mutable
                                                                {
                                                                    if (safeThis)
                                                                        safeThis->sharingFinished (success, false);
                                                                });
#endif
            }
        }
    }

    
    
    void takeSnapshot()
    {
        SafePointer<MainContentComponent> safeThis (this);
        cameraDevice->takeStillPicture ([safeThis] (const juce::Image& image) mutable { safeThis->imageReceived (image); });
    }
    
    // This is called by the camera device when a new image arrives
    void imageReceived (const juce::Image& image)
    {
        if (! image.isValid())
            return;
        
        
        const int desired_width = image.getWidth();
        const int desired_height = image.getHeight();

        
        //Converting to grayscale in JUCE first
        juce::Image imageCopy = image.createCopy();
        imageCopy.desaturate();
        
        //Loop through all pixels to get a Color object, then get the brightness from it
        //Copying direct from memory would likely be faster
        //Copy all pixels from JUCE image to Mat
        cv::Mat cvCopy = cv::Mat::zeros(cv::Size(desired_width,desired_height),CV_8UC1);
        juce::Colour pixCol;
        float pixBright;
        
        for (int row_index = 0; row_index < desired_height; row_index++){
            for (int col_index = 0; col_index < desired_width; col_index++){
                //pixCol = imageCopy.getPixelAt(col_index, row_index);
                pixBright = (imageCopy.getPixelAt(col_index, row_index)).getBrightness();
                cvCopy.at<uchar>(row_index, col_index) = 255*pixBright;
            }
        }
        
        lastSnapshot.setImage (image);
        
        //Preprocessing for box detection
        cv::GaussianBlur(cvCopy, cvCopy, cv::Size(7,7), 5,0);
        cv::Canny(cvCopy,cvCopy,50,150);
        cv::Mat kernel = getStructuringElement(cv::MORPH_RECT, cv::Size(7,7));
        cv::Mat kernel2 = getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
        cv::dilate(cvCopy, cvCopy, kernel);
        cv::erode(cvCopy, cvCopy, kernel2);
        
        //get Contours
        std::vector<cv::Point> biggest;
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        
        findContours(cvCopy,contours,hierarchy,cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        std::vector<std::vector<cv::Point>> conPoly(contours.size());
        std::vector<cv::Rect> boundRect(contours.size());
        std::string objectType;
        
        int maxArea = 0;
        
        for (int i = 0; i < contours.size(); i++){
            int area = contourArea(contours[i]);
            
            
            if (area>2000){ //Filter areas smaller than 1000
                //TODO also filter by "rectangleness" (relative width and height)
                float peri = cv::arcLength(contours[i],true);
                cv::approxPolyDP(contours[i], conPoly[i], 0.02 * peri, true);
                
                
                
                if (area > maxArea && conPoly[i].size() == 4){
                    biggest = { conPoly[i][0], conPoly[i][1], conPoly[i][2], conPoly[i][3] };
                    maxArea = area;
                    cv::drawContours(cvCopy,conPoly,i,cv::Scalar(255,0,255),4);
                }
                
            }
        }
        
        //Reorder points
        std::vector<cv::Point> newPoints;
        std::vector<int> sumPoints, subPoints;
        
        if (biggest.size() == 0){
            std::cout << "No border found!" << std::endl;
            return; // No rectangle found
        }
        
        for( int i = 0; i < biggest.size(); i++){
            sumPoints.push_back(biggest[i].x + biggest[i].y);
            subPoints.push_back(biggest[i].x - biggest[i].y);
        }
        
        newPoints.push_back(biggest[min_element(sumPoints.begin(), sumPoints.end()) - sumPoints.begin()]); //0
        newPoints.push_back(biggest[max_element(subPoints.begin(), subPoints.end()) - subPoints.begin()]); //1
        newPoints.push_back(biggest[min_element(subPoints.begin(), subPoints.end()) - subPoints.begin()]); //2
        newPoints.push_back(biggest[max_element(sumPoints.begin(), sumPoints.end()) - sumPoints.begin()]); //3
        
        if (newPoints.size() == 0){
            std::cout << "No border found!" << std::endl;
            return;
        }
        
        //Fix perspective
        cv::Point2f src[4] = { newPoints[0], newPoints[1], newPoints[2], newPoints[3]};
        cv::Point2f dst[4] = { {0.0f,0.0f}, {(float)desired_width,0.0f}, {0.0f,(float)desired_height}, {(float)desired_width,(float)desired_height} };
        cv::Mat matrix = cv::getPerspectiveTransform(src, dst);
        cv::warpPerspective(cvCopy, cvCopy, matrix, cv::Point(desired_width,desired_height));
        
        //Crop and flip
        int cropAmount = 60;
        cv::Rect roi(cropAmount,cropAmount,desired_width-(2*cropAmount),desired_height-(2*cropAmount)); //Trim (cropAmount)px from all sides
        cvCopy = cvCopy(roi);
        flip(cvCopy,cvCopy,1);
        
        //Process cropped wave
        //cv::cvtColor(cvCopy,cvCopy,cv::COLOR_BGR2GRAY); // Convert to grayscale
        GaussianBlur(cvCopy, cvCopy, cv::Size(5,3), 5,0);
        
        cv::Canny(cvCopy,cvCopy,50,150);
        kernel = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15,15));
        cv::dilate(cvCopy, cvCopy, kernel);
        kernel = getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3,3));
        cv::erode(cvCopy, cvCopy, kernel);
        
        //extract wave
        int channels = cvCopy.channels();
        int nRows = cvCopy.rows;
        int nCols = cvCopy.cols * channels;
        
        //    if (img.isContinuous()){
        //        nCols *= nRows;
        //        nRows = 1;
        //    }
        
        int i ,j;
        uchar* p;
        std::vector<std::vector<uchar>> tableOutput(nCols,std::vector<uchar>(nRows));
        
        for( i = 0; i < nRows; ++i){
            p = cvCopy.ptr<uchar>(i);
            for( j = 0; j < nCols; ++j)
            {
                tableOutput[j][i] = p[j];
            }
        }
        
        //Scan pixels
        int waveTop;
        int waveBottom;
        cv::Mat outputWave = cv::Mat::zeros(cv::Size(cvCopy.cols,cvCopy.rows),CV_8UC1);
        wave.clear();
        int rawWave;
        
        for(int i = 0; i < nCols; i++){
            //for every x pixel, search from top down until line,
            int j = 0;
            while( (j < nRows) && (!tableOutput[i][j])){
                j++;
            }
            if(j==nRows){
                //We hit the bottom of the column without finding a high value
                //wave[i] = nRows / 2; //Set wave at middle
                rawWave = nRows / 2;
                wave.push_back(0.0);
            }
            else{
                //We found a high value
                waveTop = j;
                j = nRows-1;
                while( (j >= waveTop) && (!tableOutput[i][j])){
                    j--;
                }
                //We know we're gaurenteed to find at least one high value (the same one we found while searching from top)
                waveBottom = j;
                
                //Find midpoint between found top and bottom waves, shift around 0 and scale by half of height to convert to floating point -1 to 1
                rawWave = waveTop + (waveBottom - waveTop)/2;
                
                //shift
                double result = static_cast<double>(rawWave) - (0.5 * static_cast<double>(nRows));
                //scale
                result = result / (0.5 * static_cast<double>(nRows));
                //save
                wave.push_back(result);
            }
            outputWave.at<uchar>(rawWave,i) = 255; //377?
        }
        
        //Output wave coordinates for debugging
        /*
        for(int i = 0; i < wave.size(); i++){
            std::cout << "Wave at " << i << " is " << wave[i] << std::endl;
        }
         */
        
        //Normalize scale of wave so peak hits -1 or +1
        double waveMax = *max_element(wave.begin(),wave.end());
        double waveMin = -1*(*min_element(wave.begin(),wave.end()));
        if(waveMin>waveMax){
            waveMax = waveMin;
        }
        double scaleFactor = 1.0 / waveMax;
        
        for(int i = 0; i < wave.size(); i++){
            waveTable.set(i, wave[i]*scaleFactor);
        }

        std::cout << "Wave extraction complete." << std::endl;
        
#if JUCE_CONTENT_SHARING
        auto imageFile = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("JuceCameraPhotoDemo", ".jpg");
        
        FileOutputStream stream (imageFile);
        
        if (stream.openedOk()
            && JPEGImageFormat().writeImageToStream (image, stream))
        {
            URL url (imageFile);
            
            snapshotButton   .setEnabled (false);
            recordMovieButton.setEnabled (false);
            contentSharingPending = true;
            
            SafePointer<MainContentComponent> safeThis (this);
            
            juce::ContentSharer::getInstance()->shareFiles ({url},
                                                            [safeThis] (bool success, const String&) mutable
                                                            {
                                                                if (safeThis)
                                                                    safeThis->sharingFinished (success, true);
                                                            });
        }
#endif
    }


    void errorOccurred (const juce::String& error)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                "Camera Device Error",
                                                "An error has occurred: " + error + " Camera will be closed.");
        
        cameraDevice.reset();
        
        cameraSelectorComboBox.setSelectedId (1);
        snapshotButton   .setEnabled (false);
        recordMovieButton.setEnabled (false);
    }
    
    void sharingFinished (bool success, bool isCapture)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                isCapture ? "Image sharing result" : "Video sharing result",
                                                success ? "Success!" : "Failed!");
        
        contentSharingPending = false;
        snapshotButton   .setEnabled (true);
        recordMovieButton.setEnabled (true);
    }
    
    void timerCallback() override
    {
        
    }
    
    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        currentFrequency = freqSlider.getValue();
        targetFrequency = currentFrequency;
        amplitude = ampSlider.getValue();
        phase = 0;
        wtSize = 1160;
        currentSampleRate = sampleRate;
        
        //one cycle of a sine wave
        for (int i = 0; i < wtSize; i++)
        {
            waveTable.insert(i, sin(2.0 * juce::double_Pi * i / wtSize));
        }
        
        visualiser.clear();
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        float* const leftSpeaker = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
        float* const rightSpeaker = bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);

        const double bufferSize = bufferToFill.numSamples;

        for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
        {
            leftSpeaker[sample] = waveTable[(int)phase] * amplitude;
            rightSpeaker[sample] = waveTable[(int)phase] * amplitude;
            updateFrequency(bufferSize);
        }
        
        visualiser.pushBuffer(bufferToFill);
    }

    void releaseResources() override
    {
        // This will be called when the audio device stops, or when it is being
        // restarted due to a setting change.

        // For more details, see the help for AudioProcessor::releaseResources()
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        // (Our component is opaque, so we must completely fill the background with a solid colour)
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

        // You can add your drawing code here!
        
    }

    void resized() override
    {
        const int labelSpace = 100;
        freqSlider.setBounds(labelSpace, 20, getWidth() - 100, 20);
        ampSlider.setBounds(labelSpace, 50, getWidth() - 100, 50);
        
        visualiser.setBounds(labelSpace, 110, getWidth() - 200, 200);
        
        auto r = getLocalBounds().reduced (5);
        r.setTop(340);
        
        auto top = r.removeFromTop (25);
        cameraSelectorComboBox.setBounds (top.removeFromLeft (250));
        
        r.removeFromTop (4);
        top = r.removeFromTop (25);
        
        snapshotButton.changeWidthToFitText (24);
        snapshotButton.setBounds (top.removeFromLeft (snapshotButton.getWidth()));
        top.removeFromLeft (4);
        
        r.removeFromTop (4);
        auto previewArea = shouldUseLandscapeLayout() ? r.removeFromLeft (r.getWidth() / 2)
        : r.removeFromTop (r.getHeight() / 2);
        
        if (cameraPreviewComp.get() != nullptr)
            cameraPreviewComp->setBounds (previewArea);
        
        if (shouldUseLandscapeLayout())
            r.removeFromLeft (4);
        else
            r.removeFromTop (4);
        
        lastSnapshot.setBounds (r);
        
    }

private:
    
    //==============================================================================
    
    // Your private member variables go here...
    juce::Slider freqSlider, ampSlider;
    juce::Label freqLabel, ampLabel;
   
    juce::Array<double> waveTable;
    double wtSize;
    double currentFrequency, targetFrequency;
    double phase;
    double increment;
    double amplitude;
    double currentSampleRate;
    double bufferSize;
    
    Visualiser visualiser;
    

    std::string path;
    cv::Mat img;
    
    
    std::unique_ptr<juce::CameraDevice> cameraDevice;
    std::unique_ptr<juce::Component> cameraPreviewComp;
    juce::ImageComponent lastSnapshot;
    
    juce::ComboBox cameraSelectorComboBox {"Camera" };
    juce::TextButton snapshotButton        { "Take a snapshot" };
    
#if ! JUCE_ANDROID && ! JUCE_IOS
    juce::TextButton recordMovieButton     { "Record a movie (to your desktop)..." };
#else
    TextButton recordMovieButton     { "Record a movie" };
#endif
    
    bool recordingMovie = false;
    juce::File recordingFile;
    bool contentSharingPending = false;
    
    bool customImage = false;
    
    std::vector<double> wave;
    
    void setPortraitOrientationEnabled (bool shouldBeEnabled)
    {
        auto allowedOrientations = juce::Desktop::getInstance().getOrientationsEnabled();
        
        if (shouldBeEnabled)
            allowedOrientations |= juce::Desktop::upright;
        else
            allowedOrientations &= ~juce::Desktop::upright;
        
        juce::Desktop::getInstance().setOrientationsEnabled (allowedOrientations);
    }
    
    bool shouldUseLandscapeLayout() const noexcept
    {
#if JUCE_ANDROID || JUCE_IOS
        auto orientation = Desktop::getInstance().getCurrentOrientation();
        return orientation == Desktop::rotatedClockwise || orientation == Desktop::rotatedAntiClockwise;
#else
        return false;
#endif
    }
    
    
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
};


// (This function is called by the app startup code to create our main component)
juce::Component* createMainContentComponent()     { return new MainContentComponent(); }
