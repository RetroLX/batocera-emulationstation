#include "components/VideoGstreamerComponent.h"

#include "renderers/Renderer.h"
#include "resources/TextureResource.h"
#include "utils/StringUtil.h"
#include "PowerSaver.h"
#include "Settings.h"
#include <SDL_mutex.h>
#include <cmath>
#include "SystemConf.h"
#include "ThemeData.h"
#include <SDL_timer.h>
#include "AudioManager.h"

#include <gst/app/gstappsink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <libyuv.h>

#ifdef WIN32
#include <codecvt>
#endif

#include "ImageIO.h"

#define MATHPI          3.141592653589793238462643383279502884L

static SDL_mutex* mutex;

VideoGstreamerComponent::VideoGstreamerComponent(Window* window, std::string subtitles) :
	  VideoComponent(window),
      playbin_(NULL)
    , videoBin_(NULL)
    , videoSink_(NULL)
    , videoConvert_(NULL)
    , videoConvertCaps_(NULL)
    , videoBus_(NULL)
    , texture_(NULL)
    , height_(0)
    , width_(0)
    , videoBuffer_(NULL)
    , frameReady_(false)
    , isPlaying_(false)
    , playCount_(0)
    , numLoops_(0)
    , volume_(1.0)
    , currentVolume_(0.0)
{
    mutex = SDL_CreateMutex();
	mElapsed = 0;
	mColorShift = 0xFFFFFFFF;
	mLinearSmooth = false;

	mLoops = -1;
	mCurrentLoop = 0;

	// Get an empty texture for rendering the video
	mTexture = nullptr;// TextureResource::get("");
	mEffect = VideoGstreamerFlags::VideoGstreamerEffect::BUMP;

	// Make sure Gstreamer has been initialised
    // TODO Subtitles support
    this->initialize();
}

VideoGstreamerComponent::~VideoGstreamerComponent()
{
	stopVideo();
}

void VideoGstreamerComponent::setResize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && !mTargetIsMax && !mTargetIsMin && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = false;
	mTargetIsMin = false;
	mStaticImage.setResize(width, height);
	resize();
}

void VideoGstreamerComponent::setMaxSize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && mTargetIsMax && !mTargetIsMin && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = true;
	mTargetIsMin = false;
	mStaticImage.setMaxSize(width, height);
	resize();
}

void VideoGstreamerComponent::setMinSize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && mTargetIsMin && !mTargetIsMax && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = false;
	mTargetIsMin = true;
	mStaticImage.setMinSize(width, height);
	resize();
}

void VideoGstreamerComponent::onVideoStarted()
{
	VideoComponent::onVideoStarted();
	resize();
}

void VideoGstreamerComponent::resize()
{
	if(!mTexture)
		return;

    mVideoWidth = getWidth();
    mVideoHeight = getHeight();

	const Vector2f textureSize((float)mVideoWidth, (float)mVideoHeight);

	if(textureSize == Vector2f::Zero())
		return;

		// SVG rasterization is determined by height (see SVGResource.cpp), and rasterization is done in terms of pixels
		// if rounding is off enough in the rasterization step (for images with extreme aspect ratios), it can cause cutoff when the aspect ratio breaks
		// so, we always make sure the resultant height is an integer to make sure cutoff doesn't happen, and scale width from that
		// (you'll see this scattered throughout the function)
		// this is probably not the best way, so if you're familiar with this problem and have a better solution, please make a pull request!

		if(mTargetIsMax)
		{

			mSize = textureSize;

			Vector2f resizeScale((mTargetSize.x() / mSize.x()), (mTargetSize.y() / mSize.y()));

			if(resizeScale.x() < resizeScale.y())
			{
				mSize[0] *= resizeScale.x();
				mSize[1] *= resizeScale.x();
			}else{
				mSize[0] *= resizeScale.y();
				mSize[1] *= resizeScale.y();
			}

			// for SVG rasterization, always calculate width from rounded height (see comment above)
			mSize[1] = Math::round(mSize[1]);
			mSize[0] = (mSize[1] / textureSize.y()) * textureSize.x();

		}
		else if (mTargetIsMin)
		{
			mSize = ImageIO::getPictureMinSize(textureSize, mTargetSize);
		}
		else {
			// if both components are set, we just stretch
			// if no components are set, we don't resize at all
			mSize = mTargetSize == Vector2f::Zero() ? textureSize : mTargetSize;

			// if only one component is set, we resize in a way that maintains aspect ratio
			// for SVG rasterization, we always calculate width from rounded height (see comment above)
			if(!mTargetSize.x() && mTargetSize.y())
			{
				mSize[1] = Math::round(mTargetSize.y());
				mSize[0] = (mSize.y() / textureSize.y()) * textureSize.x();
			}else if(mTargetSize.x() && !mTargetSize.y())
			{
				mSize[1] = Math::round((mTargetSize.x() / textureSize.x()) * textureSize.y());
				mSize[0] = (mSize.y() / textureSize.y()) * textureSize.x();
			}
		}

	// mSize.y() should already be rounded
	mTexture->rasterizeAt((size_t)Math::round(mSize.x()), (size_t)Math::round(mSize.y()));

	onSizeChanged();
}

void VideoGstreamerComponent::setColorShift(unsigned int color)
{
	mColorShift = color;
}

void VideoGstreamerComponent::render(const Transform4x4f& parentTrans)
{
	if (!isShowing() || !isVisible())
		return;

	VideoComponent::render(parentTrans);

	bool initFromPixels = true;

	if (!mIsPlaying)
	{
		// If video is still attached to the path & texture is initialized, we suppose it had just been stopped (onhide, ondisable, screensaver...)
		// still render the last frame
		if (mTexture != nullptr && !mVideoPath.empty() && mPlayingVideoPath == mVideoPath && mTexture->isLoaded())
			initFromPixels = false;
		else
			return;
	}

	float t = mFadeIn;
	if (mFadeIn < 1.0)
	{
		t = 1.0 - mFadeIn;
		t -= 1; // cubic ease in
		t = Math::lerp(0, 1, t*t*t + 1);
		t = 1.0 - t;
	}

	if (t == 0.0)
		return;

	Transform4x4f trans = parentTrans * getTransform();

	if (mRotation == 0 && !mTargetIsMin && !Renderer::isVisibleOnScreen(trans.translation().x(), trans.translation().y(), mSize.x() * trans.r0().x(), mSize.y() * trans.r1().y()))
		return;

	Renderer::setMatrix(trans);

	// Build a texture for the video frame
	if (initFromPixels)
	{
		//int frame = mContext.surfaceId;
		//if (mContext.hasFrame[frame])
		{
			if (mTexture == nullptr)
			{
				mTexture = TextureResource::get("", false, mLinearSmooth);

				resize();
				trans = parentTrans * getTransform();
				Renderer::setMatrix(trans);
			}

#ifdef _RPI_
			// Rpi : A lot of videos are encoded in 60fps on screenscraper
			// Try to limit transfert to opengl textures to 30fps to save CPU
			if (!Settings::getInstance()->getBool("OptimizeVideo") || mElapsed >= 40) // 40ms = 25fps, 33.33 = 30 fps
#endif
			{
				mElapsed = 0;
			}
		}
	}

	if (mTexture == nullptr)
		return;

	float opacity = (mOpacity / 255.0f) * t;

	if (hasStoryBoard())
		opacity = (mOpacity / 255.0f);

	unsigned int color = Renderer::convertColor(mColorShift & 0xFFFFFF00 | (unsigned char)((mColorShift & 0xFF) * opacity));

	Renderer::Vertex   vertices[4];

	if (mEffect == VideoGstreamerFlags::VideoGstreamerEffect::SLIDERIGHT && mFadeIn > 0.0 && mFadeIn < 1.0 && mConfig.startDelay > 0 && !hasStoryBoard())
	{
		float t = 1.0 - mFadeIn;
		t -= 1; // cubic ease in
		t = Math::lerp(0, 1, t*t*t + 1);
		//t = 1.0 - t;

		vertices[0] = { { 0.0f     , 0.0f      }, { t, 1.0f }, color };
		vertices[1] = { { 0.0f     , mSize.y() }, { t, 0.0f }, color };
		vertices[2] = { { mSize.x(), 0.0f      }, { t + 1.0f, 1.0f }, color };
		vertices[3] = { { mSize.x(), mSize.y() }, { t + 1.0f, 0.0f }, color };
	}
	else
	if (mEffect == VideoGstreamerFlags::VideoGstreamerEffect::SIZE && mFadeIn > 0.0 && mFadeIn < 1.0 && mConfig.startDelay > 0 && !hasStoryBoard())
	{
		float t = 1.0 - mFadeIn;
		t -= 1; // cubic ease in
		t = Math::lerp(0, 1, t*t*t + 1);
		t = 1.0 - t;

		float w = mSize.x() * t;
		float h = mSize.y() * t;
		float centerX = mSize.x() / 2.0f;
		float centerY = mSize.y() / 2.0f;

		Vector2f topLeft(Math::round(centerX - w / 2.0f), Math::round(centerY - h / 2.0f));
		Vector2f bottomRight(Math::round(centerX + w / 2.0f), Math::round(centerY + h / 2.0f));

		vertices[0] = { { topLeft.x()		, topLeft.y()	  }, { 0.0f, 1.0f }, color };
		vertices[1] = { { topLeft.x()		, bottomRight.y() }, { 0.0f, 0.0f }, color };
		vertices[2] = { { bottomRight.x()	, topLeft.y()     }, { 1.0f, 1.0f }, color };
		vertices[3] = { { bottomRight.x()	, bottomRight.y() }, { 1.0f, 0.0f }, color };
	}
	else if (mEffect == VideoGstreamerFlags::VideoGstreamerEffect::BUMP && mFadeIn > 0.0 && mFadeIn < 1.0 && mConfig.startDelay > 0 && !hasStoryBoard())
	{
		// Bump Effect
		float bump = sin((MATHPI / 2.0) * mFadeIn) + sin(MATHPI * mFadeIn) / 2.0;

		float w = mSize.x() * bump;
		float h = mSize.y() * bump;
		float centerX = mSize.x() / 2.0f;
		float centerY = mSize.y() / 2.0f;

		Vector2f topLeft(Math::round(centerX - w / 2.0f), Math::round(centerY - h / 2.0f));
		Vector2f bottomRight(Math::round(centerX + w / 2.0f), Math::round(centerY + h / 2.0f));

		vertices[0] = { { topLeft.x()		, topLeft.y()	  }, { 0.0f, 1.0f }, color };
		vertices[1] = { { topLeft.x()		, bottomRight.y() }, { 0.0f, 0.0f }, color };
		vertices[2] = { { bottomRight.x()	, topLeft.y()     }, { 1.0f, 1.0f }, color };
		vertices[3] = { { bottomRight.x()	, bottomRight.y() }, { 1.0f, 0.0f }, color };
	}
	else
	{
		vertices[0] = { { 0.0f     , 0.0f      }, { 0.0f, 1.0f }, color };
		vertices[1] = { { 0.0f     , mSize.y() }, { 0.0f, 0.0f }, color };
		vertices[2] = { { mSize.x(), 0.0f      }, { 1.0f, 1.0f }, color };
		vertices[3] = { { mSize.x(), mSize.y() }, { 1.0f, 0.0f }, color };
	}

	// round vertices
	for(int i = 0; i < 4; ++i)
		vertices[i].pos.round();
	
	if (mTexture->bind())
	{
		beginCustomClipRect();

		Vector2f targetSizePos = (mTargetSize - mSize) * mOrigin * -1;

		if (mTargetIsMin)
		{			
			Vector2i pos(trans.translation().x() + (int)targetSizePos.x(), trans.translation().y() + (int)targetSizePos.y());
			Vector2i size((int)(mTargetSize.x() * trans.r0().x()), (int)(mTargetSize.y() * trans.r1().y()));
			Renderer::pushClipRect(pos, size);
		}

		if (mRoundCorners > 0)
		{
			float x = 0;
			float y = 0;
			float size_x = mSize.x();
			float size_y = mSize.y();
			
			if (mTargetIsMin)
			{
				x = targetSizePos.x();
				y = targetSizePos.y();
				size_x = mTargetSize.x();
				size_y = mTargetSize.y();
			}
			
			float radius = Math::max(size_x, size_y) * mRoundCorners;
			Renderer::enableRoundCornerStencil(x, y, size_x, size_y, radius);

            mTexture->bind();
		}

		// Render it
		Renderer::drawTriangleStrips(&vertices[0], 4);

		if (mRoundCorners > 0)
			Renderer::disableStencil();

		if (mTargetIsMin)
			Renderer::popClipRect();

		endCustomClipRect();

		Renderer::bindTexture(0);
	}
}

void VideoGstreamerComponent::handleLooping()
{
    /*
	if (mIsPlaying && mMediaPlayer)
	{
		libvlc_state_t state = libvlc_media_player_get_state(mMediaPlayer);
		if (state == libvlc_Ended)
		{
			if (mLoops >= 0)
			{
				mCurrentLoop++;
				if (mCurrentLoop > mLoops)
				{
					stopVideo();

					mFadeIn = 0.0;
					mPlayingVideoPath = "";
					mVideoPath = "";
					return;
				}
			}

			if (mPlaylist != nullptr)
			{
				auto nextVideo = mPlaylist->getNextItem();
				if (!nextVideo.empty())
				{
					stopVideo();
					setVideo(nextVideo);
					return;
				}
				else
					mPlaylist = nullptr;
			}
			
			if (mVideoEnded != nullptr)
			{
				bool cont = mVideoEnded();
				if (!cont)
				{
					stopVideo();
					return;
				}
			}

			if (!getPlayAudio() || (!mScreensaverMode && !Settings::getInstance()->getBool("VideoAudio")) || (Settings::getInstance()->getBool("ScreenSaverVideoMute") && mScreensaverMode))
				libvlc_audio_set_mute(mMediaPlayer, 1);

			//libvlc_media_player_set_position(mMediaPlayer, 0.0f);
			if (mMedia)
				libvlc_media_player_set_media(mMediaPlayer, mMedia);

			libvlc_media_player_play(mMediaPlayer);
		}
	}*/
}

void VideoGstreamerComponent::startVideo()
{
	if (mIsPlaying)
		return;

	if (hasStoryBoard("", true) && mConfig.startDelay > 0)
		startStoryboard();

	mTexture = nullptr;
	mCurrentLoop = 0;
	mVideoWidth = 0;
	mVideoHeight = 0;

#ifdef WIN32
	std::string path(Utils::String::replace(mVideoPath, "/", "\\"));
#else
	std::string path(mVideoPath);
#endif
    PowerSaver::pause();
    AudioManager::setVideoPlaying(true);
    resize();

    play(mVideoPath);
    // Update the playing state -> Useless now set by display() & onVideoStarted
    mIsPlaying = true;
    mFadeIn = 0.0f;
    onVideoStarted();
/*
	// Make sure we have a video path
	if (mVLC && (path.size() > 0))
	{
		// Set the video that we are going to be playing so we don't attempt to restart it
		mPlayingVideoPath = mVideoPath;

		// Open the media
		mMedia = libvlc_media_new_path(mVLC, path.c_str());
		if (mMedia)
		{			
			// use : vlc –long-help
			// WIN32 ? libvlc_media_add_option(mMedia, ":avcodec-hw=dxva2");
			// RPI/OMX ? libvlc_media_add_option(mMedia, ":codec=mediacodec,iomx,all"); .

			std::string options = SystemConf::getInstance()->get("vlc.options");
			if (!options.empty())
			{
				std::vector<std::string> tokens = Utils::String::split(options, ' ');
				for (auto token : tokens)
					libvlc_media_add_option(mMedia, token.c_str());
			}
			
			// If we have a playlist : most videos have a fader, skip it 1 second
			if (mPlaylist != nullptr && mConfig.startDelay == 0 && !mConfig.showSnapshotDelay && !mConfig.showSnapshotNoVideo)
				libvlc_media_add_option(mMedia, ":start-time=0.7");			

			bool hasAudioTrack = false;

			unsigned track_count;
			// Get the media metadata so we can find the aspect ratio
			libvlc_media_parse(mMedia);
			libvlc_media_track_t** tracks;
			track_count = libvlc_media_tracks_get(mMedia, &tracks);
			for (unsigned track = 0; track < track_count; ++track)
			{
				if (tracks[track]->i_type == libvlc_track_audio)
					hasAudioTrack = true;
				else if (tracks[track]->i_type == libvlc_track_video)
				{
					mVideoWidth = tracks[track]->video->i_width;
					mVideoHeight = tracks[track]->video->i_height;		

					if (hasAudioTrack)
						break;
				}
			}
			libvlc_media_tracks_release(tracks, track_count);

			// Make sure we found a valid video track
			if ((mVideoWidth > 0) && (mVideoHeight > 0))
			{			
				if (Settings::getInstance()->getBool("OptimizeVideo"))
				{
					// Avoid videos bigger than resolution
					Vector2f maxSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
										
#ifdef _RPI_
					// Temporary -> RPI -> Try to limit videos to 400x300 for performance benchmark
					if (!Renderer::isSmallScreen())
						maxSize = Vector2f(400, 300);
#endif

					if (!mTargetSize.empty() && (mTargetSize.x() < maxSize.x() || mTargetSize.y() < maxSize.y()))
						maxSize = mTargetSize;

					

					// If video is bigger than display, ask VLC for a smaller image
					auto sz = ImageIO::adjustPictureSize(Vector2i(mVideoWidth, mVideoHeight), Vector2i(maxSize.x(), maxSize.y()), mTargetIsMin);
					if (sz.x() < mVideoWidth || sz.y() < mVideoHeight)
					{
						mVideoWidth = sz.x();
						mVideoHeight = sz.y();
					}
				}


				// Setup the media player
				mMediaPlayer = libvlc_media_player_new_from_media(mMedia);
			
				if (hasAudioTrack)
				{
					if (!getPlayAudio() || (!mScreensaverMode && !Settings::getInstance()->getBool("VideoAudio")) || (Settings::getInstance()->getBool("ScreenSaverVideoMute") && mScreensaverMode))
						libvlc_audio_set_mute(mMediaPlayer, 1);
					else
						AudioManager::setVideoPlaying(true);
				}

				libvlc_media_player_play(mMediaPlayer);
				libvlc_video_set_callbacks(mMediaPlayer, lock, unlock, display, (void*)&mContext);
				libvlc_video_set_format(mMediaPlayer, "RGBA", (int)mVideoWidth, (int)mVideoHeight, (int)mVideoWidth * 4);

			}
		}
	}*/
}

void VideoGstreamerComponent::stopVideo()
{
	mIsPlaying = false;
	mIsWaitingForVideoToStart = false;
	mStartDelayed = false;

	// Release the media player so it stops calling back to us
    stop();
	//mTexture = nullptr;
	PowerSaver::resume();
	AudioManager::setVideoPlaying(false);
}

void VideoGstreamerComponent::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties)
{
	VideoComponent::applyTheme(theme, view, element, properties);

	using namespace ThemeFlags;

	const ThemeData::ThemeElement* elem = theme->getElement(view, element, "video");

	if (elem && elem->has("effect"))
	{
		if (!(elem->get<std::string>("effect").compare("slideRight")))
			mEffect = VideoGstreamerFlags::VideoGstreamerEffect::SLIDERIGHT;
		else if (!(elem->get<std::string>("effect").compare("size")))
			mEffect = VideoGstreamerFlags::VideoGstreamerEffect::SIZE;
		else if (!(elem->get<std::string>("effect").compare("bump")))
			mEffect = VideoGstreamerFlags::VideoGstreamerEffect::BUMP;
		else
			mEffect = VideoGstreamerFlags::VideoGstreamerEffect::NONE;
	}

	if (elem && elem->has("roundCorners"))
		setRoundCorners(elem->get<float>("roundCorners"));
	
	if (properties & COLOR)
	{
		if (elem && elem->has("color"))
			setColorShift(elem->get<unsigned int>("color"));
		
		if (elem->has("opacity"))
			setOpacity((unsigned char)(elem->get<float>("opacity") * 255.0));
	}

	if (elem && elem->has("loops"))
		mLoops = (int)elem->get<float>("loops");
	else
		mLoops = -1;

	if (elem->has("linearSmooth"))
		mLinearSmooth = elem->get<bool>("linearSmooth");

	applyStoryboard(elem);
	mStaticImage.applyStoryboard(elem, "snapshot");
}

void VideoGstreamerComponent::update(int deltaTime)
{
	mElapsed += deltaTime;
	mStaticImage.update(deltaTime);
    updateVideo(deltaTime);
	VideoComponent::update(deltaTime);	
}

void VideoGstreamerComponent::onShow()
{
	VideoComponent::onShow();
	mStaticImage.onShow();

	if (hasStoryBoard("", true) && mConfig.startDelay > 0)
		pauseStoryboard();
}

ThemeData::ThemeElement::Property VideoGstreamerComponent::getProperty(const std::string name)
{
	Vector2f scale = getParent() ? getParent()->getSize() : Vector2f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	if (name == "size" || name == "maxSize" || name == "minSize")
		return mSize / scale;

	if (name == "color")
		return mColorShift;

	if (name == "roundCorners")
		return mRoundCorners;

	return VideoComponent::getProperty(name);
}

void VideoGstreamerComponent::setProperty(const std::string name, const ThemeData::ThemeElement::Property& value)
{
	Vector2f scale = getParent() ? getParent()->getSize() : Vector2f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	if ((name == "maxSize" || name == "minSize") && value.type == ThemeData::ThemeElement::Property::PropertyType::Pair)
	{
		mTargetSize = Vector2f(value.v.x() * scale.x(), value.v.y() * scale.y());
		resize();
	}
	else if (name == "color" && value.type == ThemeData::ThemeElement::Property::PropertyType::Int)
		setColorShift(value.i);
	else if (name == "roundCorners" && value.type == ThemeData::ThemeElement::Property::PropertyType::Float)
		setRoundCorners(value.f);
	else
		VideoComponent::setProperty(name, value);
}


/// RETRO FE GST

bool VideoGstreamerComponent::initialized_ = false;

//todo: this started out as sandbox code. This class needs to be refactored

// MUST match video size
//gboolean GStreamerVideo::busCallback(GstBus * /* bus */, GstMessage * /* msg */, gpointer /* data */)
//{
//    // this callback only needs to be defined so we can loop the video once it completes
//    return TRUE;
//}

void VideoGstreamerComponent::setNumLoops(int n)
{
    if ( n > 0 )
        numLoops_ = n;
}

void VideoGstreamerComponent::processNewBuffer (GstElement * /* fakesink */, GstBuffer *buf, GstPad *new_pad, gpointer userdata)
{
    VideoGstreamerComponent *video = (VideoGstreamerComponent *)userdata;

    SDL_LockMutex(mutex);
    if (!video->frameReady_ && video && video->isPlaying_)
    {
        if(!video->width_ || !video->height_)
        {
            GstCaps *caps = gst_pad_get_current_caps (new_pad);
            GstStructure *s = gst_caps_get_structure(caps, 0);

            gst_structure_get_int(s, "width", &video->width_);
            gst_structure_get_int(s, "height", &video->height_);
        }

        if(video->height_ && video->width_ && !video->videoBuffer_)
        {
            video->videoBuffer_ = gst_buffer_ref(buf);
            video->frameReady_ = true;
        }
    }
    SDL_UnlockMutex(mutex);
}


bool VideoGstreamerComponent::initialize()
{
    if(initialized_)
    {
        return true;
    }

    gst_init(NULL, NULL);

#ifdef WIN32
    std::string path = Utils::combinePath(Configuration::absolutePath, "Core");
    GstRegistry *registry = gst_registry_get();
    gst_registry_scan_path(registry, path.c_str());
#endif

    initialized_ = true;

    return true;
}

bool VideoGstreamerComponent::deInitialize()
{
    gst_deinit();
    initialized_ = false;
    return true;
}


bool VideoGstreamerComponent::stop()
{
    if(!initialized_)
    {
        return false;
    }

    if(videoSink_)
    {
        g_object_set(G_OBJECT(videoSink_), "signal-handoffs", FALSE, NULL);
    }

    if(playbin_)
    {
        (void)gst_element_set_state(playbin_, GST_STATE_NULL);
    }

    if(texture_)
    {
        delete[] texture_;
        texture_ = NULL;
    }

    if(videoBuffer_)
    {
        gst_buffer_unref(videoBuffer_);
        videoBuffer_ = NULL;
    }

    freeElements();

    isPlaying_ = false;
    height_ = 0;
    width_ = 0;
    frameReady_ = false;

    return true;
}

bool VideoGstreamerComponent::play(std::string file)
{
    playCount_ = 0;

    if(!initialized_)
    {
        return false;
    }

    stop();

    currentFile_ = file;

    const gchar *uriFile = gst_filename_to_uri (file.c_str(), NULL);
    if(!uriFile)
    {
        return false;
    }
    else
    {
//        Configuration::convertToAbsolutePath(Configuration::absolutePath, file);
        file = uriFile;
        delete uriFile;

        if(!playbin_)
        {
            playbin_ = gst_element_factory_make("playbin", "player");
            if(!playbin_)
            {
                LOG(LogError) << "Video" << " Could not create playbin";
                freeElements();
                return false;
            }
            videoBin_ = gst_bin_new("SinkBin");
            if(!videoBin_)
            {
                LOG(LogError) << "Video" << " Could not create videobin";
                freeElements();
                return false;
            }
            videoSink_  = gst_element_factory_make("fakesink", "video_sink");
            if(!videoSink_)
            {
                LOG(LogError) << "Video" << " Could not create video sink";
                freeElements();
                return false;
            }
            videoConvert_  = gst_element_factory_make("capsfilter", "video_convert");
            if(!videoConvert_)
            {
                LOG(LogError) << "Video" << " Could not create video converter";
                freeElements();
                return false;
            }
            videoConvertCaps_ = gst_caps_from_string("video/x-raw,format=(string)I420,pixel-aspect-ratio=(fraction)1/1");
            if(!videoConvertCaps_)
            {
                LOG(LogError) << "Video" << "Could not create video caps";
                freeElements();
                return false;
            }
            height_ = 0;
            width_ = 0;

            gst_bin_add_many(GST_BIN(videoBin_), videoConvert_, videoSink_, NULL);
            if (!gst_element_link_filtered(videoConvert_, videoSink_, videoConvertCaps_))
            {
                LOG(LogError) << "Video" << " gst_element_link_filtered failed";
                freeElements();
                return false;
            }

            GstPad *videoConvertSinkPad = gst_element_get_static_pad(videoConvert_, "sink");
            if(!videoConvertSinkPad)
            {
                LOG(LogError) << "Video" << " Could not get video convert sink pad";
                freeElements();
                return false;
            }

            g_object_set(G_OBJECT(videoSink_), "sync", TRUE, "qos", FALSE, NULL);

            GstPad *videoSinkPad = gst_ghost_pad_new("sink", videoConvertSinkPad);
            if(!videoSinkPad)
            {
                LOG(LogError) << "Video" << " Could not get video bin sink pad";
                freeElements();
                gst_object_unref(videoConvertSinkPad);
                videoConvertSinkPad = NULL;
                return false;
            }

            if (!gst_element_add_pad(videoBin_, videoSinkPad))
            {
                LOG(LogError) << "Video" << " gst_element_add_pad failed";
                freeElements();
                return false;
            }
            gst_object_unref(videoConvertSinkPad);
            videoConvertSinkPad = NULL;
        }
        g_object_set(G_OBJECT(playbin_), "uri", file.c_str(), "video-sink", videoBin_, NULL);

        isPlaying_ = true;

        g_object_set(G_OBJECT(videoSink_), "signal-handoffs", TRUE, NULL);
        g_signal_connect(videoSink_, "handoff", G_CALLBACK(processNewBuffer), this);

        videoBus_ = gst_pipeline_get_bus(GST_PIPELINE(playbin_));
        if (!videoBus_)
        {
            LOG(LogError) << "Video" << " gst_pipeline_get_bus failed";
            freeElements();
            return false;
        }
//        gst_bus_add_watch(videoBus_, &busCallback, this);

        /* Start playing */
        GstStateChangeReturn playState = gst_element_set_state(GST_ELEMENT(playbin_), GST_STATE_PLAYING);
        if (playState != GST_STATE_CHANGE_ASYNC)
        {
            isPlaying_ = false;
            std::stringstream ss;
            ss << "Unable to set the pipeline to the playing state: ";
            ss << playState;
            LOG(LogError) <<  "Video " << ss.str();
            freeElements();
            return false;
        }
    }

    gst_stream_volume_set_volume( GST_STREAM_VOLUME( playbin_ ), GST_STREAM_VOLUME_FORMAT_LINEAR, 0.0 );
    gst_stream_volume_set_mute( GST_STREAM_VOLUME( playbin_ ), true );

    return true;
}

void VideoGstreamerComponent::freeElements()
{
    if(videoBus_)
    {
        gst_object_unref(videoBus_);
        videoBus_ = NULL;
    }
    if(playbin_)
    {
        gst_object_unref(playbin_);
        playbin_ = NULL;
    }
//    if(videoSink_)
//    {
//        gst_object_unref(videoSink_);
    videoSink_ = NULL;
//    }
//    if(videoConvert_)
//    {
    //       gst_object_unref(videoConvert_);
    videoConvert_ = NULL;
//    }
    if(videoConvertCaps_)
    {
        gst_caps_unref(videoConvertCaps_);
        videoConvertCaps_ = NULL;
    }
//    if(videoBin_)
//    {
//        gst_object_unref(videoBin_);
    videoBin_ = NULL;
//    }
}


int VideoGstreamerComponent::getHeight()
{
    return static_cast<int>(height_);
}

int VideoGstreamerComponent::getWidth()
{
    return static_cast<int>(width_);
}


void VideoGstreamerComponent::draw()
{
}

void VideoGstreamerComponent::updateVideo(float /* dt */)
{
    SDL_LockMutex(mutex);
    if(!texture_ && width_ != 0 && height_ != 0)
    {
        texture_ = new unsigned char[width_ * height_ * 4];
    }

    if(playbin_)
    {
        if(volume_ > 1.0)
            volume_ = 1.0;
        if ( currentVolume_ > volume_ || currentVolume_ + 0.005 >= volume_ )
            currentVolume_ = volume_;
        else
            currentVolume_ += 0.005;
        gst_stream_volume_set_volume( GST_STREAM_VOLUME( playbin_ ), GST_STREAM_VOLUME_FORMAT_LINEAR, static_cast<double>(currentVolume_));
        if(currentVolume_ < 0.1)
            gst_stream_volume_set_mute( GST_STREAM_VOLUME( playbin_ ), true );
        else
            gst_stream_volume_set_mute( GST_STREAM_VOLUME( playbin_ ), false );
    }

    if(videoBuffer_)
    {
        GstVideoMeta *meta;
        meta = gst_buffer_get_video_meta(videoBuffer_);

        // Presence of meta indicates non-contiguous data in the buffer
        if (!meta)
        {
            void *pixels;
            int pitch;
            unsigned int vbytes = width_ * height_;
            vbytes += (vbytes / 2);
            gsize bufSize = gst_buffer_get_size(videoBuffer_);
            if (bufSize == vbytes)
            {
                GstMapInfo bufInfo;
                gst_buffer_map(videoBuffer_, &bufInfo, GST_MAP_READ);
                unsigned char* yuv = bufInfo.data;
                gst_buffer_extract(videoBuffer_, 0, yuv, vbytes);
                unsigned int y_stride, u_stride, v_stride;
                const Uint8 *y_plane, *u_plane, *v_plane;
                y_stride = width_;
                u_stride = v_stride = y_stride / 2;
                y_plane = yuv;
                u_plane = y_plane + (height_ * y_stride);
                v_plane = u_plane + ((height_ / 2) * u_stride);
                libyuv::I420ToABGR(y_plane,
                                   y_stride,
                                   u_plane,
                                   u_stride,
                                   v_plane,
                                   v_stride,
                                   texture_,
                                   width_ * 4,
                                   width_,
                                   height_);
                gst_buffer_unmap(videoBuffer_, &bufInfo);
                mVideoWidth = getWidth();
                mVideoHeight = getHeight();
                if ((texture_) && (mVideoWidth > 0) && (mVideoHeight > 0))
                    mTexture->updateFromExternalPixels(texture_, mVideoWidth, mVideoHeight);
                frameReady_ = false;
            }
            else
            {
                GstMapInfo bufInfo;
                unsigned int y_stride, u_stride, v_stride;
                const Uint8 *y_plane, *u_plane, *v_plane;
                std::stringstream ss;

                y_stride = GST_ROUND_UP_4(width_);
                u_stride = v_stride = GST_ROUND_UP_4(y_stride / 2);

                gst_buffer_map(videoBuffer_, &bufInfo, GST_MAP_READ);
                y_plane = bufInfo.data;
                u_plane = y_plane + (height_ * y_stride);
                v_plane = u_plane + ((height_ / 2) * u_stride);
                libyuv::I420ToABGR(y_plane,
                                   y_stride,
                                   u_plane,
                                   u_stride,
                                   v_plane,
                                   v_stride,
                                   texture_,
                                   width_ * 4,
                                   width_,
                                   height_);
                gst_buffer_unmap(videoBuffer_, &bufInfo);
                mVideoWidth = getWidth();
                mVideoHeight = getHeight();
                if ((texture_) && (mVideoWidth > 0) && (mVideoHeight > 0))
                    mTexture->updateFromExternalPixels(texture_, mVideoWidth, mVideoHeight);
                frameReady_ = false;
            }
        }
        else
        {
            GstMapInfo y_info, u_info, v_info;
            unsigned char *y_plane, *u_plane, *v_plane;
            int y_stride, u_stride, v_stride;

            gst_video_meta_map(meta, 0, &y_info, (gpointer*)&y_plane, &y_stride, GST_MAP_READ);
            gst_video_meta_map(meta, 1, &u_info, (gpointer*)&u_plane, &u_stride, GST_MAP_READ);
            gst_video_meta_map(meta, 2, &v_info, (gpointer*)&v_plane, &v_stride, GST_MAP_READ);
            libyuv::I420ToABGR(y_plane,
                               y_stride,
                               u_plane,
                               u_stride,
                               v_plane,
                               v_stride,
                               texture_,
                               width_ * 4,
                               width_,
                               height_);
            gst_video_meta_unmap(meta, 0, &y_info);
            gst_video_meta_unmap(meta, 1, &u_info);
            gst_video_meta_unmap(meta, 2, &v_info);
            mVideoWidth = getWidth();
            mVideoHeight = getHeight();
            if ((texture_) && (mVideoWidth > 0) && (mVideoHeight > 0))
                mTexture->updateFromExternalPixels(texture_, mVideoWidth, mVideoHeight);
            frameReady_ = false;
        }

        gst_buffer_unref(videoBuffer_);
        videoBuffer_ = NULL;
    }

    if(videoBus_)
    {
        GstMessage *msg = gst_bus_pop(videoBus_);
        if(msg)
        {
            if(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS)
            {
                playCount_++;

                //todo: nesting hazard
                // if number of loops is 0, set to infinite (todo: this is misleading, rename variable)
                if(!numLoops_ || numLoops_ > playCount_)
                {
                    gst_element_seek(playbin_,
                                     1.0,
                                     GST_FORMAT_TIME,
                                     GST_SEEK_FLAG_FLUSH,
                                     GST_SEEK_TYPE_SET,
                                     0,
                                     GST_SEEK_TYPE_NONE,
                                     GST_CLOCK_TIME_NONE);
                }
                else
                {
                    isPlaying_ = false;
                }
            }

            gst_message_unref(msg);
        }
    }
    SDL_UnlockMutex(mutex);
}


bool VideoGstreamerComponent::isPlaying()
{
    return isPlaying_;
}


void VideoGstreamerComponent::setVolume(float volume)
{
    volume_ = volume;
}
