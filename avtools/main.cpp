//
//  main.cpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/7.
//

#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include "avformat.hpp"

using std::cout;
using std::cerr;
using std::endl;

int remux(const char* infile, const char* outfile) {
  AVPacket* packet = NULL;
  int ret = 0;

  packet = av_packet_alloc();
  if (!packet) {
    cerr << "fail to alloc AVPacket\n";
    return -1;
  }

  try {
    avtools::iFormat ifmt(infile);
    avtools::oFormat ofmt(outfile);

    AVFormatContext* ic = ifmt.ctx();
    AVFormatContext* oc = ofmt.ctx();

    AVStream** istreams = ic->streams;
    int nr_istreams = ic->nb_streams;

    std::map<int, int> stream_mapping;

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

      // modify packet fields
      packet->stream_index = ost->index;
      av_packet_rescale_ts(packet, ist->time_base, ost->time_base);
      packet->pos = -1;

      // write frame to output format
      if (ofmt.interleaved_write_frame(packet) < 0) {
        throw std::runtime_error("fail to write frame");
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
  int rc = 0;

  if (argc != 3) {
    cerr << "Usage: ./avtools <input.mp4> <output.flv>\n";
    exit(EXIT_FAILURE);
  }

  rc = remux(argv[1], argv[2]);
  cout << "done, rc=" << rc << endl;

  return 0;
}
