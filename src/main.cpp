#include <iostream>
#include <fstream> // For checking the video device
#include <string>  // For constructing device paths

#ifdef GST_FOUND // Conditional compilation
#include <gst/gst.h>
#endif

using namespace std;

int main(int argc, char *argv[]) {
#ifdef GST_FOUND
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <receiver_ip_address>" << endl;
        return -1;
    }

    string receiver_ip = argv[1];

    GstElement *pipeline, *videosrc, *x264enc, *rtph264pay, *udpsink;
    GstStateChangeReturn ret;
    string video_device_path;

    // Check for available video devices
    bool device_found = false;
    for (int i = 0; i < 100; ++i) {
        video_device_path = "/dev/video" + to_string(i);
        ifstream video_device(video_device_path);
        if (video_device) {
            device_found = true;
            clog << "Found video device: " << video_device_path << endl;
            video_device.close();
            break;
        }
    }

    if (!device_found) {
        cerr << "ERROR: No video device found." << endl;
        return -1;
    }

    // Initialize GStreamer
    gst_init(NULL, NULL);

    // Create pipeline elements
    pipeline = gst_pipeline_new("my-pipeline");
    if (!pipeline) {
        cerr << "ERROR: Pipeline could not be created." << endl;
        return -1;
    }

    videosrc = gst_element_factory_make("v4l2src", "source");
    if (!videosrc) {
        cerr << "ERROR: Video source element could not be created." << endl;
        gst_object_unref(pipeline);
        return -1;
    }

    x264enc = gst_element_factory_make("v4l2h264enc", "encoder");  // Hardware H.264 encoder
    if (!x264enc) {
        cerr << "ERROR: x264 encoder element could not be created." << endl;
        gst_object_unref(pipeline);
        gst_object_unref(videosrc);
        return -1;
    }

    rtph264pay = gst_element_factory_make("rtph264pay", "payloader"); // RTP packetizer
    if (!rtph264pay) {
        cerr << "ERROR: RTP payloader element could not be created." << endl;
        gst_object_unref(pipeline);
        gst_object_unref(videosrc);
        gst_object_unref(x264enc);
        return -1;
    }

    udpsink = gst_element_factory_make("udpsink", "sink"); // UDP sink
    if (!udpsink) {
        cerr << "ERROR: UDP sink element could not be created." << endl;
        gst_object_unref(pipeline);
        gst_object_unref(videosrc);
        gst_object_unref(x264enc);
        gst_object_unref(rtph264pay);
        return -1;
    }

    // Set properties
    g_object_set(videosrc, "device", video_device_path.c_str(), NULL); // Use the first found video device
    g_object_set(udpsink, "host", receiver_ip.c_str(), NULL); // Use the provided receiver's IP address
    g_object_set(udpsink, "port", 5000, NULL);      // Port for UDP streaming

    g_object_set(x264enc, "control-rate", 1, NULL);  // 1=constant bitrate
    g_object_set(x264enc, "target-bitrate", 500000, NULL);  // 500 kbps (note: in bits/sec, not kbps)


    // Build the pipeline (link elements)
    gst_bin_add_many(GST_BIN(pipeline), videosrc, x264enc, rtph264pay, udpsink, NULL);
    if (!gst_element_link_many(videosrc, x264enc, rtph264pay, udpsink, NULL)) {
        cerr << "ERROR: Elements could not be linked." << endl;
        gst_object_unref(pipeline);
        return -1;
    }

    // Start playing
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        cerr << "ERROR: Unable to set the pipeline to the playing state." << endl;
        gst_object_unref(pipeline);
        return -1;
    }

    // --- Message Handling (Keep the pipeline running) ---
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)); // Wait for EOS or error

    // Parse messages
    if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                cerr << "ERROR received from element " << GST_OBJECT_NAME(msg->src) << ": " << err->message << endl;
                cerr << "Debugging information: " << (debug_info ? debug_info : "none") << endl;
                g_clear_error(&err);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                cout << "End-Of-Stream reached." << endl;
                break;
            default:
                // We should not reach here because we only asked for ERROR and EOS
                cerr << "Unexpected message received." << endl;
                break;
        }
        gst_message_unref(msg);
    }


    // --- Cleanup ---
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(bus);  // Clean up the bus
    return 0;

#else
    cerr << "GStreamer not found during compilation.  Streaming is disabled." << endl;
    return -1;
#endif
}
