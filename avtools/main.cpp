//
//  main.cpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/7.
//

#include <cstdlib>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <thread>
#include <gflags/gflags.h>
#include "avformat.hpp"

using std::cout;
using std::cerr;
using std::endl;

DEFINE_string(a, "dump", "action");
DEFINE_string(f, "", "output format");

static bool ValidateAction(const char* flagname, const std::string& value) {
  if (value == "dump" || value == "remux") {
    return true;
  }
  cerr << "Possible values for '-" << flagname << "' are: \"dump\", \"remux\".\n";
  return false;
}
DEFINE_validator(a, &ValidateAction);

class FFProbe {
 public:
  using on_streams_cb = std::function<void(const AVStream* const*, int)>;
  using on_packet_cb = std::function<void(AVPacket*)>;

  FFProbe(const char* url, const on_streams_cb& on_streams, const on_packet_cb& on_packet)
      : ifmt_(url), on_streams_(on_streams), on_packet_(on_packet) {
    on_construct();
  }

  FFProbe(const char* url, on_streams_cb&& on_streams, on_packet_cb&& on_packet)
      : ifmt_(url), on_streams_(std::move(on_streams)), on_packet_(std::move(on_packet)) {
    on_construct();
  }

  virtual ~FFProbe() {
    av_packet_free(&packet_);
  }

  int next() {
    int rc = ifmt_.read_frame(packet_);
    if (rc == 0) {
      on_packet_(packet_);
      av_packet_unref(packet_);
    }
    return rc;
  }

 private:
  void on_construct() {
    if (!(packet_ = av_packet_alloc())) {
      throw std::runtime_error("fail to alloc AVPacket");
    }
    on_streams_(ifmt_.ctx()->streams, ifmt_.ctx()->nb_streams);
  }

  avtools::AVInputFormat ifmt_;
  on_streams_cb on_streams_;
  on_packet_cb on_packet_;
  AVPacket* packet_ = NULL;
};

int remux(const char* infile, const char* outfile,
          const char* outfmt = NULL,
          bool real_time = false) {
  AVPacket* packet = NULL;
  int ret = 0;

  packet = av_packet_alloc();
  if (!packet) {
    cerr << "fail to alloc AVPacket\n";
    return -1;
  }

  try {
    using clock_type = std::chrono::steady_clock;

    avtools::AVInputFormat ifmt(infile);
    avtools::AVOutputFormat ofmt(outfile, outfmt);

    AVFormatContext* ic = ifmt.ctx();
    AVFormatContext* oc = ofmt.ctx();

    AVStream** istreams = ic->streams;
    int nr_istreams = ic->nb_streams;

    std::map<int, int> stream_mapping;

    auto start_time = clock_type::now();
    int64_t time_elapsed_ms = 0;
    int64_t time_span_ms = 0;

    uint64_t pkt_cnt = 0;

    // dump input format
    av_dump_format(ic, 0, infile, 0);

    // create output stream
    for (int i = 0; i < nr_istreams; ++i) {
      AVStream* ist = istreams[i];
      AVStream* ost = NULL;

      if (ist->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
          ist->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        continue;
      }

      ost = ofmt.new_stream();
      if (!ost) {
        throw std::runtime_error("fail to create stream");
      }
      if (avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) {
        throw std::runtime_error("fail to set codec param");
      }
      ost->codecpar->codec_tag = 0;

      stream_mapping[ist->index] = ost->index;
    }

    // dump output format
    av_dump_format(oc, 0, outfile, 1);

    // write header
    if (ofmt.write_header() < 0) {
      throw std::runtime_error("fail to write header");
    }

    // write frame
    while (1) {
      int rc = 0;
      AVStream* ist = NULL;
      AVStream* ost = NULL;

      // read frame from input format
      rc = ifmt.read_frame(packet);
      if (rc < 0) {
        if (rc != AVERROR_EOF) {
          throw std::runtime_error("fail to read frame");
        }
        break;
      }

      // check stream mapping
      if (stream_mapping.find(packet->stream_index) == stream_mapping.end()) {
        av_packet_unref(packet);
        continue;
      }
      ist = istreams[packet->stream_index];
      ost = oc->streams[stream_mapping[ist->index]];

      time_span_ms = packet->pts * av_q2d(ist->time_base) * 1000;

      // modify packet fields
      packet->stream_index = ost->index;
      av_packet_rescale_ts(packet, ist->time_base, ost->time_base);
      packet->pos = -1;

      // write frame to output format
      if (ofmt.interleaved_write_frame(packet) < 0) {
        throw std::runtime_error("fail to write frame");
      }
      ++pkt_cnt;

      time_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - start_time).count();
      if (time_span_ms - time_elapsed_ms > 1000) {
        if (real_time) {
          int64_t sleep_time_ms = (time_span_ms - time_elapsed_ms) * 4 / 5;
          cout << pkt_cnt << " frames processed\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
        }
      }
    }
  } catch (const std::exception& e) {
    cerr << e.what() << endl;
    ret = -1;
  }

  av_packet_free(&packet);

  return ret;
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 2 && argc != 3) {
    cerr << "Usage: ./avtools [-a {dump|remux}] [-f <out_fmt>] <input_url> [output_url]\n";
    exit(EXIT_FAILURE);
  }

  return 0;
}
