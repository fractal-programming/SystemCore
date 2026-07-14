/*
  This file is part of the DSP-Crowd project
  https://www.dsp-crowd.com

  Author(s):
      - Johannes Natter, office@dsp-crowd.com

  File created on 28.02.2026

  Copyright (C) 2026, Johannes Natter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef PROC_CONF_H
#define PROC_CONF_H

#ifndef CONFIG_PROC_HAVE_LIB_C_CUSTOM
#define CONFIG_PROC_HAVE_LIB_C_CUSTOM			0
#endif

#ifndef CONFIG_PROC_HAVE_LOG
#define CONFIG_PROC_HAVE_LOG					0
#endif

#ifndef CONFIG_PROC_HAVE_CORE_LOG
#define CONFIG_PROC_HAVE_CORE_LOG				0
#endif

#ifndef CONFIG_PROC_HAVE_DRIVERS
#if defined(__STDCPP_THREADS__)
#define CONFIG_PROC_HAVE_DRIVERS				1
#else
#define CONFIG_PROC_HAVE_DRIVERS				0
#endif
#endif

#ifndef CONFIG_PROC_HAVE_GLOBAL_DESTRUCTORS
#define CONFIG_PROC_HAVE_GLOBAL_DESTRUCTORS		1
#endif

#ifndef CONFIG_PROC_NUM_MAX_GLOBAL_DESTRUCTORS
#define CONFIG_PROC_NUM_MAX_GLOBAL_DESTRUCTORS	20
#endif

#ifndef CONFIG_PROC_HAVE_LIB_STD_C
#define CONFIG_PROC_HAVE_LIB_STD_C				1
#endif

#ifndef CONFIG_PROC_HAVE_LIB_STD_CPP
#if defined(__unix__) || defined(_WIN32) || defined(__APPLE__)
#define CONFIG_PROC_HAVE_LIB_STD_CPP			1
#else
#define CONFIG_PROC_HAVE_LIB_STD_CPP			0
#endif
#endif

#ifndef CONFIG_PROC_USE_DRIVER_COLOR
#define CONFIG_PROC_USE_DRIVER_COLOR			1
#endif

#ifndef CONFIG_PROC_NUM_MAX_CHILDREN_DEFAULT
#define CONFIG_PROC_NUM_MAX_CHILDREN_DEFAULT		20
#endif

#ifndef CONFIG_PROC_SHOW_ADDRESS_IN_ID
#define CONFIG_PROC_SHOW_ADDRESS_IN_ID			0
#endif

#ifndef CONFIG_PROC_ID_BUFFER_SIZE
#define CONFIG_PROC_ID_BUFFER_SIZE				64
#endif

#ifndef CONFIG_PROC_INFO_BUFFER_SIZE
#define CONFIG_PROC_INFO_BUFFER_SIZE			255
#endif

#ifndef CONFIG_PROC_DISABLE_TREE_DEFAULT
#define CONFIG_PROC_DISABLE_TREE_DEFAULT		0
#endif

#ifndef CONFIG_PROC_LOG_HAVE_CHRONO
#if defined(__unix__) || defined(_WIN32) || defined(__APPLE__)
#define CONFIG_PROC_LOG_HAVE_CHRONO			1
#else
#define CONFIG_PROC_LOG_HAVE_CHRONO			0
#endif
#endif

#ifndef CONFIG_PROC_LOG_HAVE_STDOUT
#if defined(__unix__) || defined(_WIN32) || defined(__APPLE__)
#define CONFIG_PROC_LOG_HAVE_STDOUT			1
#else
#define CONFIG_PROC_LOG_HAVE_STDOUT			0
#endif
#endif

#ifndef CONFIG_PROC_IPV6_ENABLED
#define CONFIG_PROC_IPV6_ENABLED				1
#endif

#endif

