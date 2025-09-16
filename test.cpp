#include <iostream>
#include <vector>
#include <deque>
#include <cmath>
#include <dlib/opencv.h>
#include <opencv2/opencv.hpp>
#include <dlib/image_processing.h>
#include <dlib/image_processing/frontal_face_detector.h>

// FFmpeg 头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}
//计算嘴部开合程度
double getMouthOpenness(const dlib::full_object_detection& shape) {
    double vertical = std::abs(shape.part(51).y() - shape.part(57).y());
    double horizontal = std::abs(shape.part(54).x() - shape.part(48).x());
    return vertical / horizontal;
}

// 初始化FFmpeg音频解码
AVFormatContext* init_audio(const char* filename, int& audio_stream_index, AVCodecContext** codec_ctx) {
    avformat_network_init();
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) != 0) {
        std::cerr << "Could not open audio file" << std::endl;
        return nullptr;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return nullptr;
    }

    // 查找音频流
    audio_stream_index = -1;
    AVCodecParameters* codec_params = nullptr;
    for (int i = 0; i < format_ctx->nb_streams; i++) {
        codec_params = format_ctx->streams[i]->codecpar;
        if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        std::cerr << "No audio stream found" << std::endl;
        return nullptr;
    }

    // 获取解码器
    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec: " << codec_params->codec_id << std::endl;
        return nullptr;
    }

    // 创建解码器上下文
    *codec_ctx = avcodec_alloc_context3(codec);
    if (!*codec_ctx) {
        std::cerr << "Failed to allocate codec context" << std::endl;
        return nullptr;
    }

    if (avcodec_parameters_to_context(*codec_ctx, codec_params) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return nullptr;
    }

    if (avcodec_open2(*codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return nullptr;
    }

    std::cout << "Audio initialized: "
        << "codec=" << codec->name
        << ", channels=" << codec_params->channels
        << ", sample_rate=" << codec_params->sample_rate
        << ", format=" << av_get_sample_fmt_name((AVSampleFormat)codec_params->format)
        << std::endl;

    return format_ctx;
}

// 提取音频能量
double extract_audio_energy(AVFormatContext* format_ctx, AVCodecContext* codec_ctx,
    int audio_stream_index, double frame_time,
    SwrContext*& swr_ctx, AVPacket* packet, AVFrame* frame) {
    static int64_t last_audio_pts = AV_NOPTS_VALUE;
    double energy = 0.0;
    int samples_count = 0;

    // 初始化重采样器
    if (!swr_ctx) {
        swr_ctx = swr_alloc();
        av_opt_set_int(swr_ctx, "in_channel_layout", codec_ctx->channel_layout, 0);
        av_opt_set_int(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
        av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
        av_opt_set_int(swr_ctx, "out_sample_rate", 16000, 0);
        av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", (AVSampleFormat)codec_ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        if (swr_init(swr_ctx) < 0) {
            std::cerr << "Failed to initialize resampler" << std::endl;
            return 0.0;
        }
    }

    // 计算目标时间戳（转换为音频流的时间基准）
    AVStream* audio_stream = format_ctx->streams[audio_stream_index];
    AVRational av_time_base_q = { 1, AV_TIME_BASE };

    int64_t target_pts = av_rescale_q(static_cast<int64_t>(frame_time * AV_TIME_BASE),
        av_time_base_q,
        audio_stream->time_base);

    // 读取音频包直到达到或超过目标时间
    while (last_audio_pts == AV_NOPTS_VALUE || last_audio_pts < target_pts) {
        int ret = av_read_frame(format_ctx, packet);
        if (ret < 0) {
            // 文件结束或错误
            break;
        }

        if (packet->stream_index != audio_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        // 发送数据包到解码器
        ret = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            std::cerr << "Error sending packet to decoder" << std::endl;
            continue;
        }

        // 接收解码后的帧
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            else if (ret < 0) {
                std::cerr << "Error during decoding" << std::endl;
                break;
            }

            last_audio_pts = frame->pts;

            // 重采样为单声道
            uint8_t* resampled_buffer = nullptr;
            int out_linesize;
            int out_samples = av_rescale_rnd(swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                16000, codec_ctx->sample_rate, AV_ROUND_UP);

            int res = av_samples_alloc(&resampled_buffer, &out_linesize, 1,
                                      out_samples, AV_SAMPLE_FMT_FLT, 0);
            if (res < 0) {
                std::cerr << "Failed to allocate samples" << std::endl;
                continue;
            }
            
            float* resampled_data = (float*)resampled_buffer;

            out_samples = swr_convert(swr_ctx, (uint8_t**)&resampled_data, out_samples,
                                    (const uint8_t**)frame->data, frame->nb_samples);

            // 计算音频能量 (RMS)
            for (int i = 0; i < out_samples; i++) {
                energy += resampled_data[i] * resampled_data[i];
                samples_count++;
            }

            av_freep(&resampled_buffer);
            av_frame_unref(frame);
        }
    }

    // 计算RMS能量
    if (samples_count > 0) {
        energy = std::sqrt(energy / samples_count);
    }
    else {
        // 没有新数据，返回0
        energy = 0.0;
    }

    return energy;
}

// Linux专用函数：使用FFmpeg获取精确时间戳
#ifdef __linux__
#include <libavutil/rational.h>
double get_linux_frame_time(cv::VideoCapture& cap, int64_t frame_index, double fps) {
    // 尝试使用OpenCV的时间戳
    double cv_time = cap.get(cv::CAP_PROP_POS_MSEC) / 1000.0;
    
    // 计算基于帧号的时间戳
    double frame_based_time = frame_index / fps;
    
    // 如果两者差异小于1ms，使用OpenCV的时间戳
    if (std::abs(cv_time - frame_based_time) < 0.001) {
        return cv_time;
    }
    
    // 否则使用帧号计算的时间戳
    return frame_based_time;
}
#endif

int main(int argc, char** argv) {
    const char* video_file = "x264_cbr_test_output-CCTV1.ts";

    // 初始化dlib
    dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();
    dlib::shape_predictor predictor;
    try {
        dlib::deserialize("shape_predictor_68_face_landmarks.dat") >> predictor;
    } catch (const std::exception& e) {
        std::cerr << "Error loading shape predictor: " << e.what() << std::endl;
        return -1;
    }

    // 平台特定视频捕获
    cv::VideoCapture cap;
    bool use_msmf = false;
    
    #ifdef _WIN32
        // Windows优先使用MSMF
        if (cap.open(video_file, cv::CAP_MSMF)) {
            use_msmf = true;
            std::cout << "Using MSMF backend on Windows" << std::endl;
        } else {
            std::cerr << "Failed to open with MSMF, trying FFMPEG" << std::endl;
            if (!cap.open(video_file, cv::CAP_FFMPEG)) {
                std::cerr << "Error opening video file with any backend" << std::endl;
                return -1;
            } else {
                std::cout << "Using FFMPEG backend on Windows" << std::endl;
            }
        }
    #else
        // Linux使用FFMPEG
        if (!cap.open(video_file, cv::CAP_FFMPEG)) {
            std::cerr << "Error opening video file with FFMPEG backend" << std::endl;
            return -1;
        } else {
            std::cout << "Using FFMPEG backend on Linux" << std::endl;
        }
    #endif

    // 获取视频元数据
    const double fps = cap.get(cv::CAP_PROP_FPS);
    const int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    const int64_t total_frames = static_cast<int64_t>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    const double duration = total_frames / fps;
    
    // 验证元数据有效性
    if (fps <= 0 || total_frames <= 0) {
        std::cerr << "Error: Invalid video metadata from OpenCV\n"
                  << "  FPS: " << fps << "\n"
                  << "  Frame count: " << total_frames << std::endl;
        return -1;
    }
    
    std::cout << "Video Metadata:\n"
              << "  Size: " << width << "x" << height << "\n"
              << "  FPS: " << fps << "\n"
              << "  Frame count: " << total_frames << "\n"
              << "  Duration: " << duration << "s\n";

    // 初始化音频处理
    int audio_stream_index = -1;
    AVCodecContext* audio_codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFormatContext* audio_ctx = init_audio(video_file, audio_stream_index, &audio_codec_ctx);
    if (!audio_ctx || !audio_codec_ctx) {
        std::cerr << "Audio initialization failed" << std::endl;
        cap.release();
        return -1;
    }

    // 准备音频解码所需的结构
    AVPacket* audio_packet = av_packet_alloc();
    AVFrame* audio_frame = av_frame_alloc();
    if (!audio_packet || !audio_frame) {
        std::cerr << "Failed to allocate audio structures" << std::endl;
        return -1;
    }

    // 存储嘴部运动和音频能量
    std::vector<double> mouth_openness;
    std::vector<double> audio_energy;
    std::deque<double> mouth_history;
    const int history_size = 5;//static_cast<int>(fps * 0.1); // 100ms历史

    // 创建显示窗口
    cv::namedWindow("Video and audio sync detection", cv::WINDOW_NORMAL);

    // 统一时间戳计算方式
    int64_t frame_count = 0;
    cv::Mat frame;
    while (frame_count < total_frames && cap.read(frame)) {
        double frame_time = 0.0;
        
        #ifdef _WIN32
            if (use_msmf) {
                // MSMF后端使用精确时间戳
                frame_time = cap.get(cv::CAP_PROP_POS_MSEC) / 1000.0;
            } else {
                // Windows FFMPEG后端使用帧号计算
                frame_time = frame_count / fps;
            }
        #else
            // Linux使用专用函数获取精确时间戳
            frame_time = get_linux_frame_time(cap, frame_count, fps);
        #endif
        
        // 转换为dlib图像
        dlib::cv_image<dlib::bgr_pixel> cimg(frame);

        // 人脸检测
        std::vector<dlib::rectangle> faces = detector(cimg);
        double openness = 0.0;
        bool face_detected = false;

        for (auto& face : faces) {
            // 关键点检测
            dlib::full_object_detection shape = predictor(cimg, face);
            openness = getMouthOpenness(shape);
            face_detected = true;
            
            // 绘制嘴部区域
            std::vector<cv::Point> lip_points;
            for (unsigned i = 48; i <= 67; ++i) {
                lip_points.emplace_back(shape.part(i).x(), shape.part(i).y());
            }
            cv::polylines(frame, lip_points, true, cv::Scalar(0, 255, 0), 2);
            
            // 在图像上显示开合度
            std::string text = "Openness: " + std::to_string(openness);
            cv::putText(frame, text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
        }

        // 平滑嘴部运动数据
        mouth_history.push_back(face_detected ? openness : 0.0);
        if (mouth_history.size() > history_size) {
            mouth_history.pop_front();
        }

        double smoothed_openness = 0.0;
        for (double val : mouth_history) {
            smoothed_openness += val;
        }
        smoothed_openness /= mouth_history.size();
        mouth_openness.push_back(smoothed_openness);

        // 提取音频能量 - 使用平台特定的时间戳
        double energy = extract_audio_energy(audio_ctx, audio_codec_ctx, audio_stream_index,
            frame_time, swr_ctx, audio_packet, audio_frame);
        audio_energy.push_back(energy);

        // 在图像上显示信息
        std::ostringstream time_ss;
        time_ss << "Time: " << std::fixed << std::setprecision(3) << frame_time << "s";
        cv::putText(frame, time_ss.str(), cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 255), 2);
        
        std::ostringstream frame_ss;
        frame_ss << "Frame: " << frame_count << "/" << total_frames;
        cv::putText(frame, frame_ss.str(), cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 255), 2);
        
        // 显示时间戳来源
        #ifdef _WIN32
            std::string source_text = "Timestamp: " + std::string(use_msmf ? "MSMF" : "FFMPEG");
        #else
            std::string source_text = "Timestamp: Linux FFMPEG";
        #endif
        cv::putText(frame, source_text, cv::Point(10, 120), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // 显示帧
        cv::imshow("Video and audio sync detection", frame);
        if (cv::waitKey(1) == 27) break;

        frame_count++;
        if (frame_count % 30 == 0) {
            std::cout << "Processed " << frame_count << "/" << total_frames 
                      << " frames (" << (frame_count * 100 / total_frames) << "%), "
                      << "time: " << std::fixed << std::setprecision(3) << frame_time 
                      << "s, audio energy: " << energy << std::endl;
        }
    }

    // 同步检测 - 计算嘴动与声音的延迟
    double max_corr = 0;
    int best_offset = 0;
    const int max_offset = static_cast<int>(fps * 2.5); // 最大±0.5秒偏移

    // 确保有足够的数据
    if (mouth_openness.size() < 10 || audio_energy.size() < 10) {
        std::cerr << "Insufficient data for sync detection" << std::endl;
    } else {
        // 归一化数据
        double mouth_mean = 0, audio_mean = 0;
        for (size_t i = 0; i < mouth_openness.size(); i++) {
            mouth_mean += mouth_openness[i];
            audio_mean += audio_energy[i];
        }
        mouth_mean /= mouth_openness.size();
        audio_mean /= audio_energy.size();
        
        // 计算相关性
        for (int offset = -max_offset; offset <= max_offset; ++offset) {
            double corr = 0;
            int valid_count = 0;

            for (size_t i = 0; i < mouth_openness.size(); ++i) {
                if (i + offset >= 0 && i + offset < audio_energy.size()) {
                    double mouth_val = mouth_openness[i] - mouth_mean;
                    double audio_val = audio_energy[i + offset] - audio_mean;
                    corr += mouth_val * audio_val;
                    valid_count++;
                }
            }

            if (valid_count > 0) {
                corr /= valid_count;
                if (corr > max_corr) {
                    max_corr = corr;
                    best_offset = offset;
                }
            }
        }

        // 输出结果
        double delay_ms = (best_offset * 1000.0) / fps;
        std::cout << "\n=== Sync Detection Result ===" << std::endl;
        std::cout << "Best offset: " << best_offset << " frames" << std::endl;
        std::cout << "Estimated AV delay: " << delay_ms << " ms" << std::endl;
        std::cout << "Correlation: " << max_corr << std::endl;

        if (std::abs(delay_ms) < 40) {
            std::cout << "Result: Audio and Video are in sync" << std::endl;
        } else if (delay_ms > 0) {
            std::cout << "Result: Audio is ahead of video" << std::endl;
        } else {
            std::cout << "Result: Video is ahead of audio" << std::endl;
        }
    }

    // 跨平台清理
    cap.release();
    cv::destroyAllWindows();

    if (swr_ctx) swr_free(&swr_ctx);
    if (audio_packet) av_packet_free(&audio_packet);
    if (audio_frame) av_frame_free(&audio_frame);
    if (audio_codec_ctx) avcodec_free_context(&audio_codec_ctx);
    if (audio_ctx) avformat_close_input(&audio_ctx);

    // 跨平台暂停
    #ifdef _WIN32
        system("pause");
    #else
        std::cout << "Press Enter to exit...";
        std::cin.ignore();
    #endif
    
    return 0;
}