//
//  main.cpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/7.
//

#include <cmath>
#include <cstdlib>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <thread>
#include "avformat.hpp"
#include "ffmpeg_helper.hpp"

using std::cout;
using std::cerr;
using std::endl;

int streaming(const char* url, const char* inputs[]) {
  avtools::AVOutputFormat ofmt;
  AVStream* ost = NULL;
  int h264_stream_index = -1;
  int64_t frame_cnt = 0;
  int64_t time_span = 0;
  auto start_time = std::chrono::steady_clock::now();

  auto on_streams = [&h264_stream_index](const AVStream* const* streams,
                                         int nr_streams) {
    for (int i = 0; i < nr_streams; ++i) {
      if (streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
        h264_stream_index = i;
        break;
      }
    }
  };

  auto on_packet = [&h264_stream_index, &time_span, &ost, &ofmt](AVPacket* packet) {
    if (packet->stream_index != h264_stream_index) {
      return;
    }
    packet->pts = time_span;
    packet->dts = time_span;
    packet->duration = 0;
    packet->stream_index = ost->index;
    packet->pos = -1;
    if (ofmt.interleaved_write_frame(packet) < 0) {
      throw std::runtime_error("fail to write frame");
    }
  };

  try {
    ofmt.open(url, "flv");

    ost = ofmt.new_stream();
    if (!ost) {
      throw std::runtime_error("fail to create video stream");
    }
    ost->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    ost->codecpar->codec_id = AV_CODEC_ID_H264;
    ost->codecpar->width = 1920;
    ost->codecpar->height = 1080;

    if (ofmt.write_header() < 0) {
      throw std::runtime_error("fail to write header");
    }

    for (int i = 0; inputs[i] != NULL; ++i) {
      h264_stream_index = -1;
      avtools::FFProbe prober(inputs[i], on_streams, on_packet);
      if (h264_stream_index < 0) {
        cerr << "no h264 stream found in " << inputs[i] << endl;
        continue;
      }

      while (prober.next() == 0) {
        auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
        ++frame_cnt;
        time_span = round(static_cast<double>(frame_cnt) * 1000.0 / 60.0);
        if ((time_span - time_elapsed) > 3000) {
          auto sleep_time = (time_span - time_elapsed) * 4 / 5;
          cout << frame_cnt << " frames sent, sleep for " << sleep_time << " ms...\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
      }
    }
  } catch (const std::exception& e) {
    cerr << e.what() << endl;
    return -1;
  }

  return 0;
}

int parse_stream(const char* infile) {
  std::ifstream ifs(infile, std::ifstream::binary);
  if (!ifs) {
    cerr << "Fail to open input file\n";
    return -1;
  }

  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    cerr << "Fail to alloc AVFrame\n";
    return -1;
  }

  try {
    int frame_cnt = 0;
    avtools::AVDecoder decoder(AV_CODEC_ID_GIF);

    auto on_packet = [&frame, &frame_cnt, &decoder](AVPacket* packet) {
      ++frame_cnt;

      if (decoder.send_packet(packet) != 0) {
        cerr << "Error sending packt\n";
        return;
      }

      for (int i = 0; ; ++i) {
        int rc = decoder.receive_frame(frame);
        if (rc < 0) {
          if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
            cerr << "Error receiving frame\n";
          }
          break;
        }
        cout << "frame[" << frame_cnt << ":" << i << "] "
             << frame->width << "*" << frame->height << " "
             << frame->format << endl;
      }
    };

    avtools::FFParser parser(ifs, decoder, on_packet);

    while (!parser.next());

    cout << frame_cnt << " frames parsed\n";

  } catch (const std::exception& e) {
    cerr << e.what() << endl;
    av_frame_free(&frame);
    return -1;
  }

  av_frame_free(&frame);

  return 0;
}

int main(int argc, const char* argv[]) {
  int rc = 0;

  if (argc != 2) {
    cerr << "Usage: ./avtools <input.gif>\n";
    exit(EXIT_FAILURE);
  }

  rc = parse_stream(argv[1]);

  cout << "done, rc=" << rc << endl;

  return 0;
}
