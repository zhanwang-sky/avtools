//
//  avformat.hpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/7.
//

#ifndef avformat_hpp
#define avformat_hpp

#include <cassert>
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
}

namespace avtools {

class AVFormat {
 public:
  AVFormat(const AVFormat&) = delete;
  AVFormat(AVFormat&&) = delete;
  AVFormat& operator=(const AVFormat&) = delete;
  AVFormat& operator=(AVFormat&&) = delete;

  AVFormatContext* ctx() const {
    return ctx_;
  }

 protected:
  AVFormat() { }

  virtual ~AVFormat() { }

  AVFormatContext* ctx_ = NULL;
};

class AVInputFormat : public AVFormat {
 public:
  AVInputFormat() { }

  AVInputFormat(const char* url) {
    open(url);
  }

  virtual ~AVInputFormat() {
    close();
  }

  void open(const char* url) {
    const char* err_msg = NULL;

    assert(!ctx_);

    if (avformat_open_input(&ctx_, url, NULL, NULL) < 0) {
      err_msg = "fail to open input stream";
      goto err_exit;
    }

    if (avformat_find_stream_info(ctx_, NULL) < 0) {
      err_msg = "cannot find stream info";
      goto err_exit;
    }

    return;

  err_exit:
    close();
    throw std::runtime_error(err_msg);
  }

  void close() {
    avformat_close_input(&ctx_);
  }

  int read_frame(AVPacket* packet) {
    assert(ctx_);
    return av_read_frame(ctx_, packet);
  }
};

class AVOutputFormat : public AVFormat {
 public:
  AVOutputFormat() { }

  AVOutputFormat(const char* filename, const char* format = NULL) {
    open(filename, format);
  }

  virtual ~AVOutputFormat() {
    close();
  }

  void open(const char* filename, const char* format = NULL) {
    const char* err_msg = NULL;

    assert(!ctx_);

    if (avformat_alloc_output_context2(&ctx_, NULL, format, filename) < 0) {
      err_msg = "cannot deduce output format";
      goto err_exit;
    }

    if (!(ctx_->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&ctx_->pb, filename, AVIO_FLAG_WRITE) < 0) {
        err_msg = "fail to open output file";
        goto err_exit;
      }
      need_close_ = true;
    }

    return;

  err_exit:
    close();
    throw std::runtime_error(err_msg);
  }

  void close() {
    if (ctx_) {
      if (need_trailer_) {
        av_write_trailer(ctx_);
        need_trailer_ = false;
      }
      if (need_close_) {
        avio_closep(&ctx_->pb);
        need_close_ = false;
      }
      avformat_free_context(ctx_);
      ctx_ = NULL;
    }
  }

  AVStream* new_stream() {
    assert(ctx_);
    return avformat_new_stream(ctx_, NULL);
  }

  int write_header(AVDictionary** opt = NULL) {
    int rc = 0;

    assert(ctx_);

    rc = avformat_write_header(ctx_, opt);
    if (rc >= 0) {
      need_trailer_ = true;
    }

    return rc;
  }

  int interleaved_write_frame(AVPacket* packet) {
    assert(ctx_);
    return av_interleaved_write_frame(ctx_, packet);
  }

  int write_frame(AVPacket* packet) {
    assert(ctx_);
    return av_write_frame(ctx_, packet);
  }

 private:
  bool need_close_ = false;
  bool need_trailer_ = false;
};

}

#endif /* avformat_hpp */
