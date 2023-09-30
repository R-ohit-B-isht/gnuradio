/* -*- c++ -*- */
/*
 * Copyright 2012,2014-2015 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "freq_sink_impl.h"

#include <gnuradio/io_signature.h>
#include <gnuradio/prefs.h>

#include <qwt_symbol.h>
#include <volk/volk.h>


#include <cstring>

namespace gr {
namespace qtgui {

template <class T>
typename freq_sink<T>::sptr freq_sink<T>::make(int fftsize,
                                               int wintype,
                                               double fc,
                                               double bw,
                                               const std::string& name,
                                               int nconnections,
                                               QWidget* parent)
{
    return gnuradio::make_block_sptr<freq_sink_impl<T>>(
        fftsize, wintype, fc, bw, name, nconnections, parent);
}

template <class T>
freq_sink_impl<T>::freq_sink_impl(int fftsize,
                                  int wintype,
                                  double fc,
                                  double bw,
                                  const std::string& name,
                                  int nconnections,
                                  QWidget* parent)
    : sync_block("freq_sink",
                 io_signature::make(0, nconnections, sizeof(T)),
                 io_signature::make(0, 0, 0)),
      d_fftsize(fftsize),
      d_fft_shift(fftsize),
      d_fftavg(1.0),
      d_wintype((fft::window::win_type)(wintype)),
      d_window_normalize(wintype & (1 << 15)),
      d_center_freq(fc),
      d_bandwidth(bw),
      d_name(name),
      d_nconnections(nconnections),
      d_port(pmt::mp("freq")),
      d_port_bw(pmt::mp("bw")),
      d_parent(parent)
{
    // setup bw input port
    this->message_port_register_in(d_port_bw);
    this->set_msg_handler(d_port_bw,
                          [this](pmt::pmt_t msg) { this->handle_set_bw(msg); });

    // setup output message port to post frequency when display is
    // double-clicked
    this->message_port_register_out(d_port);
    this->message_port_register_in(d_port);
    this->set_msg_handler(d_port, [this](pmt::pmt_t msg) { this->handle_set_freq(msg); });

    // setup PDU handling input port
    this->message_port_register_in(pmt::mp("in"));
    this->set_msg_handler(pmt::mp("in"),
                          [this](pmt::pmt_t msg) { this->handle_pdus(msg); });

    d_fft = std::make_unique<fft::fft_complex_fwd>(d_fftsize);
    d_fbuf.resize(d_fftsize);

    // save the last "connection" for the PDU memory
    for (int i = 0; i < d_nconnections + 1; i++) {
        d_residbufs.emplace_back(d_fftsize);
        d_magbufs.emplace_back(d_fftsize);
    }

    d_pdu_magbuf = d_magbufs[d_magbufs.size() - 1].data();

    this->buildwindow();

    this->initialize();

    this->set_trigger_mode(TRIG_MODE_FREE, 0, 0);
}

template <class T>
freq_sink_impl<T>::~freq_sink_impl()
{
    if (!d_main_gui->isClosed())
        d_main_gui->close();
}

template <class T>
bool freq_sink_impl<T>::check_topology(int ninputs, int noutputs)
{
    return ninputs == d_nconnections;
}

template <class T>
void freq_sink_impl<T>::initialize()
{
    if (qApp != NULL) {
        this->d_qApplication = qApp;
    } else {
        this->d_qApplication = new QApplication(d_argc, &d_argv);
    }

    // If a style sheet is set in the prefs file, enable it here.
    check_set_qss(this->d_qApplication);

    int numplots = (d_nconnections > 0) ? d_nconnections : 1;
    d_main_gui = new FreqDisplayForm(numplots, d_parent);
    set_fft_window(d_wintype);
    set_fft_size(d_fftsize);
    set_frequency_range(d_center_freq, d_bandwidth);

    if (!d_name.empty())
        set_title(d_name);

    this->set_output_multiple(d_main_gui->MAX_FFT_SIZE);

    // initialize update time to 10 times a second
    set_update_time(0.1);
}

template <class T>
void freq_sink_impl<T>::exec_()
{
    this->d_qApplication->exec();
}

template <class T>
QWidget* freq_sink_impl<T>::qwidget()
{
    return d_main_gui;
}

template <class T>
void freq_sink_impl<T>::set_fft_size(const int fftsize)
{
    if ((fftsize >= d_main_gui->MIN_FFT_SIZE) && (fftsize <= d_main_gui->MAX_FFT_SIZE))
        d_main_gui->setFFTSize(fftsize);
    else {
        this->d_logger->info("FFT size must be >= {} and <= {}. \nFalling back to {}.",
                             d_main_gui->MIN_FFT_SIZE,
                             d_main_gui->MAX_FFT_SIZE,
                             d_main_gui->FFT_DEFAULT_SIZE);
        d_main_gui->setFFTSize(d_main_gui->FFT_DEFAULT_SIZE);
    }
}

template <class T>
int freq_sink_impl<T>::fft_size() const
{
    return d_fftsize;
}

template <class T>
void freq_sink_impl<T>::set_fft_average(const float fftavg)
{
    d_main_gui->setFFTAverage(fftavg);
}

template <class T>
float freq_sink_impl<T>::fft_average() const
{
    return d_fftavg;
}

template <class T>
void freq_sink_impl<T>::set_fft_window(const fft::window::win_type win)
{
    d_main_gui->setFFTWindowType(win);
}

template <class T>
fft::window::win_type freq_sink_impl<T>::fft_window()
{
    return d_wintype;
}

template <class T>
void freq_sink_impl<T>::set_fft_window_normalized(const bool enable)
{
    d_window_normalize = enable;
    buildwindow();
}

template <class T>
void freq_sink_impl<T>::set_frequency_range(const double centerfreq,
                                            const double bandwidth)
{
    d_center_freq = centerfreq;
    d_bandwidth = bandwidth;
    d_main_gui->setFrequencyRange(d_center_freq, d_bandwidth);
}

template <class T>
void freq_sink_impl<T>::set_y_axis(double min, double max)
{
    d_main_gui->setYaxis(min, max);
}

template <class T>
void freq_sink_impl<T>::set_y_label(const std::string& label, const std::string& unit)
{
    d_main_gui->setYLabel(label, unit);
}

template <class T>
void freq_sink_impl<T>::set_update_time(double t)
{
    // convert update time to ticks
    gr::high_res_timer_type tps = gr::high_res_timer_tps();
    d_update_time = t * tps;
    d_main_gui->setUpdateTime(t);
    d_last_time = 0;
}

template <class T>
void freq_sink_impl<T>::set_title(const std::string& title)
{
    d_main_gui->setTitle(title.c_str());
}

template <class T>
void freq_sink_impl<T>::set_line_label(unsigned int which, const std::string& label)
{
    d_main_gui->setLineLabel(which, label.c_str());
}

template <class T>
void freq_sink_impl<T>::set_line_color(unsigned int which, const std::string& color)
{
    d_main_gui->setLineColor(which, color.c_str());
}

template <class T>
void freq_sink_impl<T>::set_line_width(unsigned int which, int width)
{
    d_main_gui->setLineWidth(which, width);
}

template <class T>
void freq_sink_impl<T>::set_line_style(unsigned int which, int style)
{
    d_main_gui->setLineStyle(which, (Qt::PenStyle)style);
}

template <class T>
void freq_sink_impl<T>::set_line_marker(unsigned int which, int marker)
{
    d_main_gui->setLineMarker(which, (QwtSymbol::Style)marker);
}

template <class T>
void freq_sink_impl<T>::set_line_alpha(unsigned int which, double alpha)
{
    d_main_gui->setMarkerAlpha(which, (int)(255.0 * alpha));
}

template <class T>
void freq_sink_impl<T>::set_size(int width, int height)
{
    d_main_gui->resize(QSize(width, height));
}

template <class T>
void freq_sink_impl<T>::set_plot_pos_half(bool half)
{
    d_main_gui->setPlotPosHalf(half);
}

template <class T>
void freq_sink_impl<T>::set_trigger_mode(trigger_mode mode,
                                         float level,
                                         int channel,
                                         const std::string& tag_key)
{
    gr::thread::scoped_lock lock(this->d_setlock);

    d_trigger_mode = mode;
    d_trigger_level = level;
    d_trigger_channel = channel;
    d_trigger_tag_key = pmt::intern(tag_key);
    d_triggered = false;
    d_trigger_count = 0;

    d_main_gui->setTriggerMode(d_trigger_mode);
    d_main_gui->setTriggerLevel(d_trigger_level);
    d_main_gui->setTriggerChannel(d_trigger_channel);
    d_main_gui->setTriggerTagKey(tag_key);

    _reset();
}

template <class T>
std::string freq_sink_impl<T>::title()
{
    return d_main_gui->title().toStdString();
}

template <class T>
std::string freq_sink_impl<T>::line_label(unsigned int which)
{
    return d_main_gui->lineLabel(which).toStdString();
}

template <class T>
std::string freq_sink_impl<T>::line_color(unsigned int which)
{
    return d_main_gui->lineColor(which).toStdString();
}

template <class T>
int freq_sink_impl<T>::line_width(unsigned int which)
{
    return d_main_gui->lineWidth(which);
}

template <class T>
int freq_sink_impl<T>::line_style(unsigned int which)
{
    return d_main_gui->lineStyle(which);
}

template <class T>
int freq_sink_impl<T>::line_marker(unsigned int which)
{
    return d_main_gui->lineMarker(which);
}

template <class T>
double freq_sink_impl<T>::line_alpha(unsigned int which)
{
    return (double)(d_main_gui->markerAlpha(which)) / 255.0;
}

template <class T>
void freq_sink_impl<T>::enable_menu(bool en)
{
    d_main_gui->enableMenu(en);
}

template <class T>
void freq_sink_impl<T>::enable_grid(bool en)
{
    d_main_gui->setGrid(en);
}

template <class T>
void freq_sink_impl<T>::enable_autoscale(bool en)
{
    d_main_gui->autoScale(en);
}

template <class T>
void freq_sink_impl<T>::enable_axis_labels(bool en)
{
    d_main_gui->setAxisLabels(en);
}

template <class T>
void freq_sink_impl<T>::enable_control_panel(bool en)
{
    if (en)
        d_main_gui->setupControlPanel();
    else
        d_main_gui->teardownControlPanel();
}

template <class T>
void freq_sink_impl<T>::enable_max_hold(bool en)
{
    d_main_gui->notifyMaxHold(en);
}

template <class T>
void freq_sink_impl<T>::enable_min_hold(bool en)
{
    d_main_gui->notifyMinHold(en);
}

template <class T>
void freq_sink_impl<T>::clear_max_hold()
{
    d_main_gui->clearMaxHold();
}

template <class T>
void freq_sink_impl<T>::clear_min_hold()
{
    d_main_gui->clearMinHold();
}

template <class T>
void freq_sink_impl<T>::disable_legend()
{
    d_main_gui->disableLegend();
}

template <class T>
void freq_sink_impl<T>::reset()
{
    gr::thread::scoped_lock lock(this->d_setlock);
    _reset();
}

template <class T>
void freq_sink_impl<T>::_reset()
{
    d_trigger_count = 0;

    // Reset the trigger.
    if (d_trigger_mode == TRIG_MODE_FREE) {
        d_triggered = true;
    } else {
        d_triggered = false;
    }
}

template <class T>
void freq_sink_impl<T>::fft(float* data_out, const T* data_in, int size)
{
    // float to complex conversion
    gr_complex* dst = d_fft->get_inbuf();
    for (int i = 0; i < size; i++)
        dst[i] = data_in[i];

    if (!d_window.empty()) {
        volk_32fc_32f_multiply_32fc(d_fft->get_inbuf(), dst, &d_window.front(), size);
    }

    d_fft->execute(); // compute the fft
    volk_32fc_s32f_x2_power_spectral_density_32f(
        data_out, d_fft->get_outbuf(), size, 1.0, size);

    d_fft_shift.shift(data_out, size);
}

template <class T>
bool freq_sink_impl<T>::windowreset()
{
    gr::thread::scoped_lock lock(this->d_setlock);

    fft::window::win_type newwintype;
    newwintype = d_main_gui->getFFTWindowType();
    if (d_wintype != newwintype) {
        d_wintype = newwintype;
        buildwindow();
        return true;
    }
    return false;
}

template <class T>
void freq_sink_impl<T>::buildwindow()
{
    d_window.clear();
    if (d_wintype != fft::window::WIN_NONE) {
        d_window = fft::window::build(d_wintype, d_fftsize, 6.76, d_window_normalize);
    }
}

template <class T>
bool freq_sink_impl<T>::fftresize()
{
    gr::thread::scoped_lock lock(this->d_setlock);

    int newfftsize = d_main_gui->getFFTSize();
    d_fftavg = d_main_gui->getFFTAverage();

    if (newfftsize != d_fftsize) {
        // Resize residbuf and replace data
        // +1 to handle PDU buffers
        for (int i = 0; i < d_nconnections + 1; i++) {
            d_residbufs[i].clear();
            d_residbufs[i].resize(newfftsize);
            d_magbufs[i].clear();
            d_magbufs[i].resize(newfftsize);
        }

        // Update the pointer to the newly allocated memory
        d_pdu_magbuf = d_magbufs[d_magbufs.size() - 1].data();

        // Set new fft size and reset buffer index
        // (throws away any currently held data, but who cares?)
        d_fftsize = newfftsize;
        d_index = 0;

        // Reset window to reflect new size
        buildwindow();

        // Reset FFTW plan for new size
        d_fft = std::make_unique<fft::fft_complex_fwd>(d_fftsize);

        d_fbuf.clear();
        d_fbuf.resize(d_fftsize);

        d_fft_shift.resize(d_fftsize);

        d_last_time = 0;

        this->set_output_multiple(d_fftsize);

        return true;
    }
    return false;
}

template <class T>
void freq_sink_impl<T>::check_clicked()
{
    if (d_main_gui->checkClicked()) {
        double freq = d_main_gui->getClickedFreq();
        this->message_port_pub(d_port, pmt::cons(d_port, pmt::from_double(freq)));
    }
}

template <class T>
void freq_sink_impl<T>::handle_set_freq(pmt::pmt_t msg)
{
    if (pmt::is_pair(msg)) {
        pmt::pmt_t x = pmt::cdr(msg);
        if (pmt::is_real(x)) {
            d_center_freq = pmt::to_double(x);
            this->d_qApplication->postEvent(d_main_gui,
                                            new SetFreqEvent(d_center_freq, d_bandwidth));
        }
    }
}

template <class T>
void freq_sink_impl<T>::handle_set_bw(pmt::pmt_t msg)
{
    if (pmt::is_pair(msg)) {
        pmt::pmt_t x = pmt::cdr(msg);
        if (pmt::is_real(x)) {
            d_bandwidth = pmt::to_double(x);
            this->d_qApplication->postEvent(d_main_gui,
                                            new SetFreqEvent(d_center_freq, d_bandwidth));
        }
    }
}

template <class T>
void freq_sink_impl<T>::_gui_update_trigger()
{
    trigger_mode new_trigger_mode = d_main_gui->getTriggerMode();
    d_trigger_level = d_main_gui->getTriggerLevel();
    d_trigger_channel = d_main_gui->getTriggerChannel();

    std::string tagkey = d_main_gui->getTriggerTagKey();
    d_trigger_tag_key = pmt::intern(tagkey);

    if (new_trigger_mode != d_trigger_mode) {
        d_trigger_mode = new_trigger_mode;
        _reset();
    }
}

template <class T>
void freq_sink_impl<T>::_test_trigger_tags(int start, int nitems)
{
    uint64_t nr = this->nitems_read(d_trigger_channel);
    std::vector<gr::tag_t> tags;
    this->get_tags_in_range(
        tags, d_trigger_channel, nr + start, nr + start + nitems, d_trigger_tag_key);
    if (!tags.empty()) {
        d_triggered = true;
        d_index = tags[0].offset - nr;
        d_trigger_count = 0;
    }
}

template <class T>
void freq_sink_impl<T>::_test_trigger_norm(int nitems,
                                           std::vector<volk::vector<double>> inputs)
{
    const double* in = (const double*)inputs[d_trigger_channel].data();
    for (int i = 0; i < nitems; i++) {
        d_trigger_count++;

        // Test if trigger has occurred based on the FFT magnitude and
        // channel number. Test if any value is > the level (in dBx).
        if (in[i] > d_trigger_level) {
            d_triggered = true;
            d_trigger_count = 0;
            break;
        }
    }

    // If using auto trigger mode, trigger periodically even
    // without a trigger event.
    if ((d_trigger_mode == TRIG_MODE_AUTO) && (d_trigger_count > d_fftsize)) {
        d_triggered = true;
        d_trigger_count = 0;
    }
}
template <class T>
bool freq_sink_impl<T>::start()
{
    this->set_output_multiple(d_fftsize);
    return true;
}

template <class T>
int freq_sink_impl<T>::work(int noutput_items,
                            gr_vector_const_void_star& input_items,
                            gr_vector_void_star& output_items)
{
    const T* in = (const T*)input_items[0];

    // Update the FFT size from the application
    bool updated = false;
    updated |= fftresize();
    updated |= windowreset();
    if (updated)
        return 0;

    check_clicked();
    _gui_update_trigger();

    gr::thread::scoped_lock lock(this->d_setlock);
    for (d_index = 0; d_index < noutput_items; d_index += d_fftsize) {

        if ((gr::high_res_timer_now() - d_last_time) > d_update_time) {

            // Trigger off tag, if active
            if ((d_trigger_mode == TRIG_MODE_TAG) && !d_triggered) {
                _test_trigger_tags(d_index, d_fftsize);
                if (d_triggered) {
                    // If not enough from tag position, early exit
                    if ((d_index + d_fftsize) >= noutput_items)
                        return d_index;
                }
            }

            for (int n = 0; n < d_nconnections; n++) {
                // Fill up residbuf with d_fftsize number of items
                in = (const T*)input_items[n];
                memcpy(d_residbufs[n].data(), &in[d_index], sizeof(T) * d_fftsize);

                fft(d_fbuf.data(), d_residbufs[n].data(), d_fftsize);
                for (int x = 0; x < d_fftsize; x++) {
                    d_magbufs[n][x] = (double)((1.0 - d_fftavg) * d_magbufs[n][x] +
                                               (d_fftavg)*d_fbuf[x]);
                }
                // volk_32f_convert_64f(d_magbufs[n], d_fbuf, d_fftsize);
            }

            // Test trigger off signal power in d_magbufs
            if ((d_trigger_mode == TRIG_MODE_NORM) ||
                (d_trigger_mode == TRIG_MODE_AUTO)) {
                _test_trigger_norm(d_fftsize, d_magbufs);
            }

            // If a trigger (FREE always triggers), plot and reset state
            if (d_triggered) {
                d_last_time = gr::high_res_timer_now();
                this->d_qApplication->postEvent(
                    d_main_gui, new FreqUpdateEvent(d_magbufs, d_fftsize));
                _reset();
            }
        }
    }

    return noutput_items;
}

template <class T>
void freq_sink_impl<T>::handle_pdus(pmt::pmt_t msg)
{
    size_t len;
    pmt::pmt_t dict, samples;

    // Test to make sure this is either a PDU or a uniform vector of
    // samples. Get the samples PMT and the dictionary if it's a PDU.
    // If not, we throw an error and exit.
    if (pmt::is_pair(msg)) {
        dict = pmt::car(msg);
        samples = pmt::cdr(msg);
    } else if (pmt::is_uniform_vector(msg)) {
        samples = msg;
    } else {
        throw std::runtime_error("time_sink_c: message must be either "
                                 "a PDU or a uniform vector of samples.");
    }

    len = pmt::length(samples);

    const T* in;
    if (pmt::is_f32vector(samples)) {
        in = (const T*)pmt::f32vector_elements(samples, len);
    } else if (pmt::is_c32vector(samples)) {
        in = (const T*)pmt::c32vector_elements(samples, len);
    } else {
        throw std::runtime_error("freq_sink: unknown data type "
                                 "of samples; must be float or gr_complex.");
    }

    // Plot if we're past the last update time
    if (gr::high_res_timer_now() - d_last_time > d_update_time) {
        d_last_time = gr::high_res_timer_now();

        // Update the FFT size from the application
        fftresize();
        windowreset();
        check_clicked();

        int winoverlap = 4;
        int fftoverlap = d_fftsize / winoverlap;
        float num = static_cast<float>(winoverlap * len) / static_cast<float>(d_fftsize);
        int nffts = static_cast<int>(ceilf(num));

        // Clear this as we will be accumulating in the for loop over nffts
        memset(d_pdu_magbuf, 0, sizeof(double) * d_fftsize);

        size_t min = 0;
        size_t max = std::min(d_fftsize, static_cast<int>(len));
        for (int n = 0; n < nffts; n++) {
            // Clear in case (max-min) < d_fftsize
            std::fill(std::begin(d_residbufs[d_nconnections]),
                      std::end(d_residbufs[d_nconnections]),
                      0x00);

            // Copy in as much of the input samples as we can
            memcpy(d_residbufs[d_nconnections].data(), &in[min], sizeof(T) * (max - min));

            // Apply the window and FFT; copy data into the PDU
            // magnitude buffer.
            fft(d_fbuf.data(), d_residbufs[d_nconnections].data(), d_fftsize);
            for (int x = 0; x < d_fftsize; x++) {
                d_pdu_magbuf[x] += (double)d_fbuf[x];
            }

            // Increment our indices; set max up to the number of
            // samples in the input PDU.
            min += fftoverlap;
            max = std::min(max + fftoverlap, len);
        }

        // Perform the averaging
        for (int x = 0; x < d_fftsize; x++) {
            d_pdu_magbuf[x] /= static_cast<double>(nffts);
        }

        // update gui per-pdu
        this->d_qApplication->postEvent(d_main_gui,
                                        new FreqUpdateEvent(d_magbufs, d_fftsize));
    }
}

template class freq_sink<float>;
template class freq_sink<gr_complex>;

} /* namespace qtgui */
} /* namespace gr */