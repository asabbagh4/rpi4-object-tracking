#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#ifdef GST_FOUND
#include <gst/gst.h>
#endif

using namespace std;

struct VideoDeviceInfo {
    string path;
    string card;
    string driver;
    string bus_info;
    bool is_capture;
    vector<pair<int, int>> supported_resolutions;
};

bool is_capture_device(const string& path) {
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) return false;

    struct v4l2_capability cap;
    bool is_capture = false;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) >= 0) {
        is_capture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    }

    close(fd);
    return is_capture;
}

VideoDeviceInfo get_device_info(const string& path) {
    VideoDeviceInfo info;
    info.path = path;
    info.is_capture = false;

    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) return info;

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) >= 0) {
        info.card = string(reinterpret_cast<const char*>(cap.card));
        info.driver = string(reinterpret_cast<const char*>(cap.driver));
        info.bus_info = string(reinterpret_cast<const char*>(cap.bus_info));
        info.is_capture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;

        if (info.is_capture) {
            vector<pair<int, int>> resolutions = {
                {640, 480}, {800, 600}, {1280, 720}, {1920, 1080}
            };

            for (auto res : resolutions) {
                struct v4l2_format fmt = {0};
                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width = res.first;
                fmt.fmt.pix.height = res.second;

                if (ioctl(fd, VIDIOC_TRY_FMT, &fmt) >= 0) {
                    if (abs(int(fmt.fmt.pix.width) - res.first) < 20 &&
                        abs(int(fmt.fmt.pix.height) - res.second) < 20) {
                        info.supported_resolutions.push_back({fmt.fmt.pix.width, fmt.fmt.pix.height});
                    }
                }
            }
        }
    }

    close(fd);
    return info;
}

vector<VideoDeviceInfo> find_all_video_devices() {
    vector<VideoDeviceInfo> devices;

    for (int i = 0; i < 100; ++i) {
        string path = "/dev/video" + to_string(i);
        int fd = open(path.c_str(), O_RDWR);
        if (fd >= 0) {
            close(fd);
            VideoDeviceInfo info = get_device_info(path);
            if (info.is_capture) {
                devices.push_back(info);
            }
        }
    }

    return devices;
}

string find_rpi_camera() {
    vector<VideoDeviceInfo> devices = find_all_video_devices();

    cout << "Found " << devices.size() << " video capture devices:" << endl;
    for (const auto& dev : devices) {
        cout << "Device: " << dev.path << endl;
        cout << "  Name: " << dev.card << endl;
        cout << "  Driver: " << dev.driver << endl;
        cout << "  Bus info: " << dev.bus_info << endl;
        cout << "  Supported resolutions: ";
        for (auto res : dev.supported_resolutions) {
            cout << res.first << "x" << res.second << " ";
        }
        cout << endl;
    }

    for (const auto& dev : devices) {
        string card_lower = dev.card;
        string driver_lower = dev.driver;
        transform(card_lower.begin(), card_lower.end(), card_lower.begin(), ::tolower);
        transform(driver_lower.begin(), driver_lower.end(), driver_lower.begin(), ::tolower);

        if (card_lower.find("rpicam") != string::npos ||
            card_lower.find("raspberry") != string::npos ||
            driver_lower.find("rpicam") != string::npos ||
            driver_lower.find("bcm2835") != string::npos) {
            cout << "Found Raspberry Pi camera: " << dev.path << " (" << dev.card << ")" << endl;
            return dev.path;
        }
    }

    if (!devices.empty()) {
        cout << "No specific Raspberry Pi camera found. Using first available capture device: "
             << devices[0].path << " (" << devices[0].card << ")" << endl;
        return devices[0].path;
    }

    return "";
}

int main(int argc, char *argv[]) {
#ifdef GST_FOUND
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <receiver_ip_address>" << endl;
        return -1;
    }

    string receiver_ip = argv[1];
    int width = 1280;
    int height = 720;
    int framerate = 30;
    int bitrate = 2000;

    string video_device_path = find_rpi_camera();
    if (video_device_path.empty()) {
        cerr << "ERROR: No video capture device found." << endl;
        return -1;
    }

    gst_init(NULL, NULL);

    GstElement *pipeline, *videosrc, *videoconvert, *videoscale, *capsfilter, *capsfilter2;
    GstElement *x264enc, *rtph264pay, *udpsink;
    GstCaps *caps, *caps2;
    GstStateChangeReturn ret;

    pipeline = gst_pipeline_new("video-stream-pipeline");
    videosrc = gst_element_factory_make("v4l2src", "source");
    // Capsfilter to force NV12 format
    capsfilter2 = gst_element_factory_make("capsfilter", "capsfilter2");
    videoconvert = gst_element_factory_make("videoconvert", "convert");
    videoscale = gst_element_factory_make("videoscale", "scale");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    x264enc = gst_element_factory_make("x264enc", "encoder");
    rtph264pay = gst_element_factory_make("rtph264pay", "payloader");
    udpsink = gst_element_factory_make("udpsink", "sink");


 if (!videosrc || !videoconvert || !videoscale || !capsfilter || !capsfilter2 ||
        !x264enc || !rtph264pay || !udpsink) {
        cerr << "ERROR: Not all elements could be created." << endl;
        if (pipeline) gst_object_unref(pipeline);
        return -1;
    }

    g_object_set(videosrc, "device", video_device_path.c_str(), NULL);

    // Force NV12 format using capsfilter *immediately* after v4l2src
    caps2 = gst_caps_new_simple("video/x-raw",
                                 "format", G_TYPE_STRING, "NV12",
                                 "width", G_TYPE_INT, width,
                                 "height", G_TYPE_INT, height,
                                 "framerate", GST_TYPE_FRACTION, framerate, 1,
                                 NULL);
    g_object_set(capsfilter2, "caps", caps2, NULL);
    gst_caps_unref(caps2);

    caps = gst_caps_new_simple("video/x-raw",
                                  "width", G_TYPE_INT, width,
                                  "height", G_TYPE_INT, height,
                                  "framerate", GST_TYPE_FRACTION, framerate, 1,
                                  NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);


  if (x264enc) {
        g_object_set(x264enc, "extra-controls", 
                     gst_structure_new("controls",
                                      "video_bitrate", G_TYPE_INT, bitrate * 1000,
                                      "video_bitrate_mode", G_TYPE_INT, 1,
                                      "repeat_sequence_header", G_TYPE_BOOLEAN, TRUE, 
                                       "h264_profile", G_TYPE_STRING, "high",
                                     "h264_level", G_TYPE_STRING, "4.0",
                                      NULL), NULL);
    }
    g_object_set(udpsink, "host", receiver_ip.c_str(), NULL);
    g_object_set(udpsink, "port", 5000, NULL);
    g_object_set(udpsink, "sync", FALSE, NULL);
    g_object_set(udpsink, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(pipeline),
                     videosrc,
                     capsfilter2, // Add the new capsfilter here
                     videoconvert,
                     videoscale,
                     capsfilter,
                     x264enc,
                     rtph264pay,
                     udpsink,
                     NULL);

    if (!gst_element_link_many(videosrc,
                              capsfilter2,  // Link v4l2src to the new capsfilter
                              videoconvert,
                              videoscale,
                              capsfilter,
                              x264enc,
                              rtph264pay,
                              udpsink,
                              NULL)) {
        cerr << "ERROR: Elements could not be linked." << endl;
        gst_object_unref(pipeline);
        return -1;
    }


    cout << "Starting video stream from " << video_device_path << " to " << receiver_ip << ":5000" << endl;
    cout << "Video settings: " << width << "x" << height << " @ " << framerate << "fps, " << bitrate << "kbps" << endl;
    cout << "Using SOFTWARE encoding (x264enc)" << endl;
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        cerr << "ERROR: Unable to set the pipeline to the playing state." << endl;
        gst_object_unref(pipeline);
        return -1;
    }
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = NULL;
    bool running = true;

    cout << "Streaming... Press Ctrl+C to stop." << endl;

    while (running) {
        msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));

        if (msg != NULL) {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &err, &debug_info);
                    cerr << "ERROR from element " << GST_OBJECT_NAME(msg->src) << ": " << err->message << endl;
                    cerr << "Debug info: " << (debug_info ? debug_info : "none") << endl;
                    g_clear_error(&err);
                    g_free(debug_info);
                    running = false;
                    break;

                case GST_MESSAGE_EOS:
                    cout << "End-Of-Stream reached." << endl;
                    running = false;
                    break;

                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                        GstState old_state, new_state, pending_state;
                        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                        cout << "Pipeline state changed from " << gst_element_state_get_name(old_state)
                             << " to " << gst_element_state_get_name(new_state) << endl;
                    }
                    break;

                default:
                    break;
            }
            gst_message_unref(msg);
        }
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(bus);

    return 0;
#else
    cerr << "GStreamer not found during compilation. Streaming is disabled." << endl;
    return -1;
#endif
}