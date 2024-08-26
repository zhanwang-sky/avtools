//
//  avcodec.hpp
//  avtools
//
//  Created by zhanwang-sky on 2024/8/23.
//

#ifndef avcodec_hpp
#define avcodec_hpp

#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace avtools {

class AVCodecBase {
 public:
  AVCodecBase(const AVCodecBase&) = delete;
  AVCodecBase& operator=(const AVCodecBase&) = delete;

  const AVCodec* codec() const { return codec_; }

  AVCodecContext* ctx() const { return ctx_; }

 protected:
  AVCodecBase(const AVCodec* codec,
              const AVCodecParameters* par,
              AVDictionary** opts) {
    const char* err_msg = NULL;

    if (!(codec_ = codec)) {
      err_msg = "cannot find codec";
      goto err_exit;
    }

    if (!(ctx_ = avcodec_alloc_context3(codec_))) {
      err_msg = "fail to alloc AVCodecContext";
      goto err_exit;
    }

    if (par) {
      if (avcodec_parameters_to_context(ctx_, par) < 0) {
        err_msg = "fail to copy codec params";
        goto err_exit;
      }
    }

    if (avcodec_open2(ctx_, codec_, opts) != 0) {
      err_msg = "fail to open codec";
      goto err_exit;
    }

    return;

  err_exit:
    on_destruct();
    throw std::runtime_error(err_msg);
  }

  virtual ~AVCodecBase() {
    on_destruct();
  }

  const AVCodec* codec_ = NULL;
  AVCodecContext* ctx_ = NULL;

 private:
  void on_destruct() {
    avcodec_free_context(&ctx_);
    codec_ = NULL;
  }
};

class AVEncoder : public AVCodecBase {
 public:
  AVEncoder(enum AVCodecID id,
            const AVCodecParameters* par = NULL,
            AVDictionary** opts = NULL)
      : AVCodecBase(avcodec_find_encoder(id), par, opts) { }

  AVEncoder(const char* name,
            const AVCodecParameters* par = NULL,
            AVDictionary** opts = NULL)
      : AVCodecBase(avcodec_find_encoder_by_name(name), par, opts) { }

  virtual ~AVEncoder() { }

  int send_frame(AVFrame* frame) {
    return avcodec_send_frame(ctx_, frame);
  }

  int receive_packet(AVPacket* packet) {
    return avcodec_receive_packet(ctx_, packet);
  }
};

class AVDecoder : public AVCodecBase {
 public:
  AVDecoder(enum AVCodecID id,
            const AVCodecParameters* par = NULL,
            AVDictionary** opts = NULL)
      : AVCodecBase(avcodec_find_decoder(id), par, opts) { }

  AVDecoder(const char* name,
            const AVCodecParameters* par = NULL,
            AVDictionary** opts = NULL)
      : AVCodecBase(avcodec_find_decoder_by_name(name), par, opts) { }

  virtual ~AVDecoder() { }

  int send_packet(AVPacket* packet) {
    return avcodec_send_packet(ctx_, packet);
  }

  int receive_frame(AVFrame* frame) {
    return avcodec_receive_frame(ctx_, frame);
  }
};

class AVParser {
 public:
  AVParser(const AVParser&) = delete;
  AVParser& operator=(const AVParser&) = delete;

  AVParser(AVCodecBase& codec) : codec_(codec) {
    if (!(parser_ = av_parser_init(codec_.codec()->id))) {
      throw std::runtime_error("fail to init AVCodecParserContext");
    }
  }

  virtual ~AVParser() {
    av_parser_close(parser_);
    parser_ = NULL;
  }

  int parse(uint8_t** obuf, int* osize,
            const uint8_t* ibuf, int isize,
            int64_t pts, int64_t dts, int64_t pos) {
    return av_parser_parse2(parser_, codec_.ctx(),
                            obuf, osize, ibuf, isize, pts, dts, pos);
  }

 private:
  AVCodecBase& codec_;
  AVCodecParserContext* parser_ = NULL;
};

}

#endif /* avcodec_hpp */
