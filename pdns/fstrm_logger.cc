#include <unistd.h>
#include <sys/un.h>

#include "config.h"
#include "fstrm_logger.hh"
#include "dolog.hh"

#define DNSTAP_CONTENT_TYPE		"protobuf:dnstap.Dnstap"

#ifdef HAVE_FSTRM

FrameStreamLogger::FrameStreamLogger(const int family, const std::string& address): d_family(family), d_address(address)
{
  fstrm_res res;

  try {
    d_fwopt = fstrm_writer_options_init();
    if (!d_fwopt) {
      throw std::runtime_error("FrameStreamLogger: fstrm_writer_options_init failed.");
    }

    res = fstrm_writer_options_add_content_type(d_fwopt, DNSTAP_CONTENT_TYPE, sizeof(DNSTAP_CONTENT_TYPE) - 1);
    if (res != fstrm_res_success) {
      throw std::runtime_error("FrameStreamLogger: fstrm_writer_options_add_content_type failed: " + std::to_string(res));
    }

    if (d_family == AF_UNIX) {
      struct sockaddr_un local;
      if (makeUNsockaddr(d_address, &local)) {
        throw std::runtime_error("FrameStreamLogger: Unable to use '" + d_address + "', it is not a valid UNIX socket path.");
      }

      d_uwopt = fstrm_unix_writer_options_init();
      if (!d_uwopt) {
        throw std::runtime_error("FrameStreamLogger: fstrm_unix_writer_options_init failed.");
      }

      // void return, no error checking.
      fstrm_unix_writer_options_set_socket_path(d_uwopt, d_address.c_str());

      d_writer = fstrm_unix_writer_init(d_uwopt, d_fwopt);
      if (!d_writer) {
        throw std::runtime_error("FrameStreamLogger: fstrm_unix_writer_init() failed.");
      }
  #ifdef HAVE_FSTRM_TCP_WRITER_INIT
    } else if (family == AF_INET) {
      d_twopt = fstrm_tcp_writer_options_init();
      if (!d_twopt) {
        throw std::runtime_error("FrameStreamLogger: fstrm_tcp_writer_options_init failed.");
      }

      try {
        ComboAddress ca(d_address);

        // void return, no error checking.
        fstrm_tcp_writer_options_set_socket_address(d_twopt, ca.toString().c_str());
        fstrm_tcp_writer_options_set_socket_port(d_twopt, std::to_string(ca.getPort()).c_str());
      } catch (PDNSException &e) {
        throw std::runtime_error("FrameStreamLogger: Unable to use '" + d_address + "': " + e.reason);
      }

      d_writer = fstrm_tcp_writer_init(d_twopt, d_fwopt);
      if (!d_writer) {
        throw std::runtime_error("FrameStreamLogger: fstrm_tcp_writer_init() failed.");
      }
  #endif
    } else {
      throw std::runtime_error("FrameStreamLogger: family " + std::to_string(family) + " not supported");
    }

    d_iothropt = fstrm_iothr_options_init();
    if (!d_iothropt) {
      throw std::runtime_error("FrameStreamLogger: fstrm_iothr_options_init() failed.");
    }

    res = fstrm_iothr_options_set_queue_model(d_iothropt, FSTRM_IOTHR_QUEUE_MODEL_MPSC);
    if (res != fstrm_res_success) {
      throw std::runtime_error("FrameStreamLogger: fstrm_iothr_options_set_queue_model failed: " + std::to_string(res));
    }

    d_iothr = fstrm_iothr_init(d_iothropt, &d_writer);
    if (!d_iothr) {
      throw std::runtime_error("FrameStreamLogger: fstrm_iothr_init() failed.");
    }

    d_ioqueue = fstrm_iothr_get_input_queue(d_iothr);
    if (!d_ioqueue) {
      throw std::runtime_error("FrameStreamLogger: fstrm_iothr_get_input_queue() failed.");
    }
  } catch (std::runtime_error &e) {
    this->cleanup();
    throw;
  }
}

void FrameStreamLogger::cleanup()
{
  if (d_iothr != nullptr) {
    fstrm_iothr_destroy(&d_iothr);
    d_iothr = nullptr;
  }
  if (d_iothropt != nullptr) {
    fstrm_iothr_options_destroy(&d_iothropt);
    d_iothropt = nullptr;
  }
  if (d_writer != nullptr) {
    fstrm_writer_destroy(&d_writer);
    d_writer = nullptr;
  }
  if (d_uwopt != nullptr) {
    fstrm_unix_writer_options_destroy(&d_uwopt);
    d_uwopt = nullptr;
  }
#ifdef HAVE_FSTRM_TCP_WRITER_INIT
  if (d_twopt != nullptr) {
    fstrm_tcp_writer_options_destroy(&d_twopt);
    d_twopt = nullptr;
  }
#endif
  if (d_fwopt != nullptr) {
    fstrm_writer_options_destroy(&d_fwopt);
    d_fwopt = nullptr;
  }
}

FrameStreamLogger::~FrameStreamLogger()
{
  this->cleanup();
}

void FrameStreamLogger::queueData(const std::string& data)
{
  uint8_t *frame = (uint8_t*)malloc(data.length());
  if (!frame) {
    warnlog("FrameStreamLogger: cannot allocate memory for stream.");
    return;
  }
  memcpy(frame, data.c_str(), data.length());

  fstrm_res res;
  res = fstrm_iothr_submit(d_iothr, d_ioqueue, frame, data.length(), fstrm_free_wrapper, nullptr);

  if (res == fstrm_res_success) {
    // Frame successfully queued.
  } else if (res == fstrm_res_again) {
    free(frame);
    warnlog("FrameStreamLogger: queue full, dropping.");
  } else {
    // Permanent failure.
    free(frame);
    warnlog("FrameStreamLogger: submitting to queue failed.");
  }
}

#endif /* HAVE_FSTRM */
