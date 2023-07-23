# Copyright 2010-2012 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Install the header files
########################################################################
install(
    FILES api.h
          ConstellationDisplayPlot.h
          DisplayPlot.h
          EyeDisplayPlot.h
          FrequencyDisplayPlot.h
          HistogramDisplayPlot.h
          TimeDomainDisplayPlot.h
          TimeRasterDisplayPlot.h
          VectorDisplayPlot.h
          WaterfallDisplayPlot.h
          ber_sink_b.h
          const_sink_c.h
          constellationdisplayform.h
          displayform.h
          edit_box_msg.h
          eye_sink.h
          eyecontrolpanel.h
          eyedisplayform.h
          eyedisplaysform.h
          form_menus.h
          fosphor_display.h
          fosphor_formatter.h
          freq_sink.h
          freqcontrolpanel.h
          freqdisplayform.h
          histogram_sink_f.h
          histogramdisplayform.h
          number_sink.h
          numberdisplayform.h
          plot_raster.h
          plot_waterfall.h
          qtgui_types.h
          sink.h
          spectrumUpdateEvents.h
          timeRasterGlobalData.h
          time_raster_sink.h
          time_sink.h
          timecontrolpanel.h
          timedisplayform.h
          timerasterdisplayform.h
          trigger_mode.h
          utils.h
          vector_sink_f.h
          vectordisplayform.h
          waterfallGlobalData.h
          waterfall_sink.h
          waterfalldisplayform.h
    DESTINATION ${GR_INCLUDE_DIR}/gnuradio/qtgui)
