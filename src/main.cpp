#include <iostream>

#ifdef GST_FOUND // Conditional compilation
#include <gst/gst.h>
#endif

using namespace std;

int main() {
#ifdef GST_FOUND
    GstElement *pipeline, *videosrc, *x264enc, *rtph264pay, *udpsink;
    GstStateChangeReturn ret;

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

    x264enc = gst_element_factory_make("x264enc", "encoder");  // Software H.264 encoder
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
    g_object_set(videosrc, "device", "/dev/video0", NULL); // Adjust if your webcam is not /dev/video0
    g_object_set(udpsink, "host", "127.0.0.1", NULL); // Replace with the receiver's IP address
    g_object_set(udpsink, "port", 5000, NULL);      // Port for UDP streaming

    // --- Optional: Configure the encoder (tune for performance) ---
    // These are example settings; you might need to adjust them.
    g_object_set(x264enc, "bitrate", 500, NULL); // Bitrate in kbps
    g_object_set(x264enc, "tune", "zerolatency", NULL); // Optimize for low latency
    // g_object_set(x264enc, "speed-preset", "ultrafast", NULL); //  Another way to control speed/quality

    // --- Optional:  Set caps on the source (v4l2src) ---
    // This is HIGHLY RECOMMENDED to avoid unnecessary format conversions.
    // Get the capabilities of your camera using `v4l2-ctl --list-formats-ext`
    // Example (replace with your camera's actual supported formats):
    // GstCaps *caps = gst_caps_new_simple("video/x-raw",
    //                                     "format", G_TYPE_STRING, "YUY2",
    //                                     "width", G_TYPE_INT, 640,
    //                                     "height", G_TYPE_INT, 480,
    //                                     "framerate", GST_TYPE_FRACTION, 30, 1,
    //                                     NULL);
    // g_object_set(videosrc, "caps", caps, NULL);
    // gst_caps_unref(caps); // Important: Release the caps object


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
