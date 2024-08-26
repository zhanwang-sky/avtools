//
//  ffmpeg_helper.hpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/18.
//

#ifndef ffmpeg_helper_hpp
#define ffmpeg_helper_hpp

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <h264bitstream/h264_stream.h>
#include "avcodec.hpp"
#include "avformat.hpp"

namespace avtools {

class NALDumper {
 public:
  NALDumper() : h_(h264_new()) {
    if (!h_) {
      throw std::runtime_error("fail to init h264bitstream library");
    }
  }

  virtual ~NALDumper() {
    h264_free(h_);
  }

  void dump(uint8_t* nal_buf, int nal_size) {
    for (int pos = 0, nal_start = 0, nal_end = 0;
         pos < nal_size &&
         find_nal_unit(nal_buf + pos, nal_size - pos, &nal_start, &nal_end) != 0;
         pos += nal_end) {
      printf(">>> [%d,%d) / (%d)\n", pos + nal_start, pos + nal_end, nal_size);
      if (read_nal_unit(h_, nal_buf + pos + nal_start, nal_end - nal_start) < 0) {
        continue;
      }
      debug_nal(h_, h_->nal);
    }
    printf("\n");
  }

 private:
  h264_stream_t* h_;
};

class FFProbe {
 public:
  using on_streams_cb = std::function<void(const AVStream* const*, int)>;
  using on_packet_cb = std::function<void(AVPacket* packet)>;

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
    if (!rc) {
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

  AVInputFormat ifmt_;
  on_streams_cb on_streams_;
  on_packet_cb on_packet_;
  AVPacket* packet_ = NULL;
};

class FFParser {
 public:
  using on_packet_cb = std::function<void(AVPacket*)>;

  FFParser(std::istream& is, AVCodecBase& codec, const on_packet_cb& on_packet)
      : is_(is), codec_(codec), on_packet_(on_packet), parser_(codec) {
    on_construct();
  }

  FFParser(std::istream& is, AVCodecBase& codec, on_packet_cb&& on_packet)
      : is_(is), codec_(codec), on_packet_(std::move(on_packet)), parser_(codec) {
    on_construct();
  }

  virtual ~FFParser() {
    on_destruct();
  }

  int next() {
    uint8_t* data = NULL;
    int size = 0;
    int eof = 0;

    is_.read(reinterpret_cast<char*>(buf_.data()), buf_.size());
    if (is_.fail() && !is_.eof()) {
      return -1;
    }

    data = buf_.data();
    size = static_cast<int>(is_.gcount());
    eof = !size;

    while (size > 0 || eof) {
      int nparsed = parser_.parse(&packet_->data, &packet_->size, data, size,
                                  AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (nparsed < 0) {
        return -1;
      }

      data += nparsed;
      size -= nparsed;

      if (packet_->size > 0) {
        on_packet_(packet_);
      }

      if (eof) {
        break;
      }
    }

    return eof;
  }

 private:
  void on_construct() {
    if (!(packet_ = av_packet_alloc())) {
      throw std::runtime_error("fail to alloc AVPacket");
    }
  }

  void on_destruct() {
    av_packet_free(&packet_);
  }

  std::istream& is_;
  AVCodecBase& codec_;
  on_packet_cb on_packet_;
  AVParser parser_;
  AVPacket* packet_ = NULL;
  std::array<uint8_t, 4096> buf_;
};

}

#endif /* ffmpeg_helper_hpp */
