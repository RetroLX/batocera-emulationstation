#pragma once
#ifndef ES_CORE_COMPONENTS_VIDEO_GSTREAMER_COMPONENT_H
#define ES_CORE_COMPONENTS_VIDEO_GSTREAMER_COMPONENT_H

#include "VideoComponent.h"
#include "ThemeData.h"
#include <mutex>

#include <gst/app/gstappsink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <SDL_render.h>

namespace VideoGstreamerFlags
{
	enum VideoGstreamerEffect
	{
		NONE,
		BUMP,
		SIZE,
		SLIDERIGHT
	};
}

class VideoGstreamerComponent : public VideoComponent
{
	// Structure that groups together the configuration of the video component
	struct Configuration
	{
		unsigned						startDelay;
		bool							showSnapshotNoVideo;
		bool							showSnapshotDelay;
		std::string						defaultVideoPath;
	};

public:
	static void setupVLC(std::string subtitles);

	VideoGstreamerComponent(Window* window, std::string subtitles="");
	virtual ~VideoGstreamerComponent();

	void render(const Transform4x4f& parentTrans) override;

	// Resize the video to fit this size. If one axis is zero, scale that axis to maintain aspect ratio.
	// If both are non-zero, potentially break the aspect ratio.  If both are zero, no resizing.
	// Can be set before or after a video is loaded.
	// setMaxSize() and setResize() are mutually exclusive.
	void setResize(float width, float height);

	// Resize the video to be as large as possible but fit within a box of this size.
	// Can be set before or after a video is loaded.
	// Never breaks the aspect ratio. setMaxSize() and setResize() are mutually exclusive.
	void setMaxSize(float width, float height);
	void setMinSize(float width, float height);

	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties);
	virtual void update(int deltaTime);

	void	setColorShift(unsigned int color);

	virtual void onShow() override;

	ThemeData::ThemeElement::Property getProperty(const std::string name) override;
	void setProperty(const std::string name, const ThemeData::ThemeElement::Property& value) override;

	void setEffect(VideoGstreamerFlags::VideoGstreamerEffect effect) { mEffect = effect; }

	bool getLinearSmooth() { return mLinearSmooth; }
	void setLinearSmooth(bool value = true) { mLinearSmooth = value; }

private:
	// Calculates the correct mSize from our resizing information (set by setResize/setMaxSize).
	// Used internally whenever the resizing parameters or texture change.
	void resize();
	// Start the video Immediately
	virtual void startVideo();
	// Stop the video
	virtual void stopVideo();
	// Handle looping the video. Must be called periodically
	virtual void handleLooping();

	virtual void onVideoStarted();

private:
    GstElement *playbin_;
    GstElement *videoBin_;
    GstElement *videoSink_;
    GstElement *videoConvert_;
    GstCaps *videoConvertCaps_;
    GstBus *videoBus_;
    gint height_;
    gint width_;
    GstBuffer *videoBuffer_;

    std::shared_ptr<TextureResource> mTexture;

	std::string					    mSubtitlePath;
	std::string					    mSubtitleTmpFile;

	VideoGstreamerFlags::VideoGstreamerEffect	mEffect;

	unsigned int					mColorShift;
	int								mElapsed;

	int								mCurrentLoop;
	int								mLoops;

	bool							mLinearSmooth;

public:
    bool initialize();
    bool play(std::string file);
    bool stop();
    bool deInitialize();
    void updateVideo(float dt);
    void draw();
    void setNumLoops(int n);
    void freeElements();
    int getHeight();
    int getWidth();
    bool isPlaying();
    void setVolume(float volume);

private:
    static void processNewBuffer (GstElement *fakesink, GstBuffer *buf, GstPad *pad, gpointer data);
    static gboolean busCallback(GstBus *bus, GstMessage *msg, gpointer data);

    unsigned char* texture_;
    bool frameReady_;
    bool isPlaying_;
    static bool initialized_;
    int playCount_;
    std::string currentFile_;
    int numLoops_;
    float volume_;
    double currentVolume_;
};

#endif // ES_CORE_COMPONENTS_VIDEO_GSTREAMER_COMPONENT_H
