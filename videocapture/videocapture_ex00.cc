#include "webrtc/examples/videocapture/videocapture_ex00.h"

#include <utility>
#include <vector>

namespace
{
	gboolean OnDestroyedCallback(GtkWidget* widget, GdkEvent *event, gpointer data)
	{
		reinterpret_cast<MainWnd*>(data)->OnDestroyed(widget, event);
		return FALSE;
	}
	void OnClickedCallback(GtkWidget *widget, GdkEvent *event, gpointer data)
	{
		reinterpret_cast<MainWnd*>(data)->OnClicked(widget);
	}
	gboolean Update(gpointer data)
	{
		MainWnd *wnd = reinterpret_cast<MainWnd*>(data);
		wnd->OnUpdate();
		return false;
	}
}
//-----------------------------------------------------------------------------------
MainWnd::MainWnd()
{
	m_window = NULL;
	m_canvas = NULL;
	m_button = NULL;
}
//-----------------------------------------------------------------------------------
MainWnd::~MainWnd()
{
	ASSERT(!IsWindow());
}
//-----------------------------------------------------------------------------------
bool MainWnd::IsWindow()
{
	return m_window != NULL && GTK_IS_WINDOW(m_window);
}
//-----------------------------------------------------------------------------------
bool MainWnd::Create()
{
	ASSERT(m_window == NULL);
	m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (m_window)
	{
		gtk_window_set_position(GTK_WINDOW(m_window), GTK_WIN_POS_CENTER);
		gtk_window_set_default_size(GTK_WINDOW(m_window), 300, 100);
		gtk_window_set_title(GTK_WINDOW(m_window), "Video Capture");
		g_signal_connect(G_OBJECT(m_window), "delete-event", G_CALLBACK(&OnDestroyedCallback), this);

		ASSERT(IsWindow());
		
		gtk_container_set_border_width(GTK_CONTAINER(m_window), 10);
		
		m_button = gtk_button_new_with_label("Start Capturing");
		gtk_widget_set_size_request(m_button, 100, 30);
		g_signal_connect(m_button, "clicked", G_CALLBACK(OnClickedCallback), this);
		gtk_container_add(GTK_CONTAINER(m_window), m_button);

		gtk_widget_show_all(m_window);

		gtk_main();
	}
	return m_window != NULL;
}
//-----------------------------------------------------------------------------------
void MainWnd::OnDestroyed(GtkWidget *widget, GdkEvent *event)
{
	m_video_source->Stop();
	m_window = NULL;
	m_canvas = NULL;
	m_button = NULL;
	gtk_main_quit();
}
//-----------------------------------------------------------------------------------
void MainWnd::OnClicked(GtkWidget *widget)
{
	AddStreams();
}
//-----------------------------------------------------------------------------------
cricket::VideoCapturer* MainWnd::OpenVideoCaptureDevice()
{
	rtc::scoped_ptr<cricket::DeviceManagerInterface> dev_manager(
			cricket::DeviceManagerFactory::Create());
	if (!dev_manager->Init())
	{
		LOG(LS_ERROR) << "Can't create device manager";
		return NULL;
	}
	std::vector<cricket::Device> devs;
	if (!dev_manager->GetVideoCaptureDevices(&devs))
	{
		LOG(LS_ERROR) << "Can't enumerate video devices";
		return NULL;
	}
	std::vector<cricket::Device>::iterator dev_it = devs.begin();
	cricket::VideoCapturer* capturer = NULL;
	for (; dev_it != devs.end(); ++dev_it)
	{
		capturer = dev_manager->CreateVideoCapturer(*dev_it);
		if (capturer != NULL) break;
	}
	return capturer;
}
//-----------------------------------------------------------------------------------
void MainWnd::AddStreams()
{
	pc_factory       = webrtc::CreatePeerConnectionFactory();
	m_video_capturer = OpenVideoCaptureDevice();
	m_video_source   = pc_factory->CreateVideoSource(m_video_capturer, NULL);
	m_video_track    = pc_factory->CreateVideoTrack("video_label", m_video_source);

	m_renderer.reset(new VideoRenderer(this, m_video_track));
	
	ASSERT(m_canvas == NULL);
	gtk_container_set_border_width(GTK_CONTAINER(m_window), 0);
	if (m_button)
	{
		gtk_widget_destroy(m_button);
		m_button = NULL;
	}
	m_canvas = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(m_window), m_canvas);
	gtk_widget_show_all(m_window);
}
//-----------------------------------------------------------------------------------
MainWnd::VideoRenderer::VideoRenderer(MainWnd *main_wnd, webrtc::VideoTrackInterface* track_to_render)
	: width_(0), height_(0), main_wnd_(main_wnd), m_video_track(track_to_render)
{
	m_video_track->AddRenderer(this);
}
//-----------------------------------------------------------------------------------
MainWnd::VideoRenderer::~VideoRenderer()
{
	m_video_track->RemoveRenderer(this);
}
//-----------------------------------------------------------------------------------
void MainWnd::VideoRenderer::SetSize(int width, int height)
{
	gdk_threads_enter();
	
	if (width_ == width && height_ == height) {return;}
	
	width_ = width; height_ = height;
	image_.reset(new uint8_t[width * height * 4]);
	
	gdk_threads_leave();
}
//-----------------------------------------------------------------------------------
void MainWnd::VideoRenderer::RenderFrame(const cricket::VideoFrame *video_frame)
{
    gdk_threads_enter();

    const cricket::VideoFrame* frame = video_frame->GetCopyWithRotationApplied();

    SetSize(static_cast<int>(frame->GetWidth()),
            static_cast<int>(frame->GetHeight()));

    int size = width_ * height_ * 4;

    // TODO: Convert directly to RGBA
    frame->ConvertToRgbBuffer(cricket::FOURCC_ARGB,
                              image_.get(),
                              size,
                              width_ * 4);

    // Convert the B,G,R,A frame to R,G,B,A, which is accepted by GTK.
    // The 'A' is just padding for GTK, so we can use it as temp.
    uint8_t* pix = image_.get();
    uint8_t* end = image_.get() + size;
    while (pix < end)
    {
        pix[3] = pix[0];    // Save B to A.
        pix[0] = pix[2];    // Set Red.
        pix[2] = pix[3];    // Set Blue.
        pix[3] = 0xFF;      // Fixed Alpha.
        pix += 4;
    }

    gdk_threads_leave();

    g_idle_add(Update, main_wnd_);
}
//-----------------------------------------------------------------------------------
void MainWnd::OnUpdate() 
{
	gdk_threads_enter();

	VideoRenderer* renderer_ = m_renderer.get();

	if (renderer_ && renderer_->image() != NULL && m_canvas != NULL)
	{
		int width = renderer_->width();
		int height = renderer_->height();

		if (!m_image_buffer.get())
		{
			m_image_buffer_size = (width * height * 4); // 开辟一片内存空间以便存储一帧视频图片
			m_image_buffer.reset(new uint8_t[m_image_buffer_size]);
			gtk_widget_set_size_request(m_canvas, width, height);
		}

		const uint32_t* image = reinterpret_cast<const uint32_t*>(renderer_->image()); // 从renderer获取一帧视频图像的地址

		uint32_t* scaled = reinterpret_cast<uint32_t*>(m_image_buffer.get());
		for (int row = 0; row < height; ++row)
		{
			for (int col = 0; col < width; ++col)
			{
				int x = col;
				scaled[x] = image[col];
			}
			image += width;
			scaled += width;
		}

		gdk_draw_rgb_32_image(m_canvas->window,
							m_canvas->style->fg_gc[GTK_STATE_NORMAL],
							0,
							0,
							width,
							height,
							GDK_RGB_DITHER_MAX,
							m_image_buffer.get(),
							width * 4);
	}
	gdk_threads_leave();
}
//-----------------------------------------------------------------------------------




