#ifndef CPU_FREQUENCY_HPP
#define CPU_FREQUENCY_HPP
/*
 *         OpenPBS (Portable Batch System) v2.3 Software License
 *
 * Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
 * All rights reserved.
 *
 * ---------------------------------------------------------------------------
 * For a license to use or redistribute the OpenPBS software under conditions
 * other than those described below, or to purchase support for this software,
 * please contact Veridian Systems, PBS Products Department ("Licensor") at:
 *
 *    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
 *                        877 902-4PBS (US toll-free)
 * ---------------------------------------------------------------------------
 *
 * This license covers use of the OpenPBS v2.3 software (the "Software") at
 * your site or location, and, for certain users, redistribution of the
 * Software to other sites and locations.  Use and redistribution of
 * OpenPBS v2.3 in source and binary forms, with or without modification,
 * are permitted provided that all of the following conditions are met.
 * After December 31, 2001, only conditions 3-6 must be met:
 *
 * 1. Commercial and/or non-commercial use of the Software is permitted
 *    provided a current software registration is on file at www.OpenPBS.org.
 *    If use of this software contributes to a publication, product, or
 *    service, proper attribution must be given; see www.OpenPBS.org/credit.html
 *
 * 2. Redistribution in any form is only permitted for non-commercial,
 *    non-profit purposes.  There can be no charge for the Software or any
 *    software incorporating the Software.  Further, there can be no
 *    expectation of revenue generated as a consequence of redistributing
 *    the Software.
 *
 * 3. Any Redistribution of source code must retain the above copyright notice
 *    and the acknowledgment contained in paragraph 6, this list of conditions
 *    and the disclaimer contained in paragraph 7.
 *
 * 4. Any Redistribution in binary form must reproduce the above copyright
 *    notice and the acknowledgment contained in paragraph 6, this list of
 *    conditions and the disclaimer contained in paragraph 7 in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 5. Redistributions in any form must be accompanied by information on how to
 *    obtain complete source code for the OpenPBS software and any
 *    modifications and/or additions to the OpenPBS software.  The source code
 *    must either be included in the distribution or be available for no more
 *    than the cost of distribution plus a nominal fee, and all modifications
 *    and additions to the Software must be freely redistributable by any party
 *    (including Licensor) without restriction.
 *
 * 6. All advertising materials mentioning features or use of the Software must
 *    display the following acknowledgment:
 *
 *     "This product includes software developed by NASA Ames Research Center,
 *     Lawrence Livermore National Laboratory, and Veridian Information
 *     Solutions, Inc.
 *     Visit www.OpenPBS.org for OpenPBS software support,
 *     products, and information."
 *
 * 7. DISCLAIMER OF WARRANTY
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
 * ARE EXPRESSLY DISCLAIMED.
 *
 * IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
 * U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This license will be governed by the laws of the Commonwealth of Virginia,
 * without reference to its choice of law rules.
 */

#include <vector>
#include <string>
#include "attribute.h"


/* The reference for this file is
 *  https://www.kernel.org/doc/Documentation/cpu-freq/user-guide.txt
 */

class cpu_frequency
  {
private:

  std::string path;                    //Path to cpufreq directory for this cpu.

  bool valid;                        //This is set to true if all values are loaded.

  unsigned long cpu_min_frequency;  //cpuinfo_min_freq :       this file shows the minimum operating
  //                         frequency the processor can run at(in kHz)
  unsigned long cpu_max_frequency;  //cpuinfo_max_freq :        this file shows the maximum operating
  //                           frequency the processor can run at(in kHz)
  std::vector<cpu_frequency_type> available_governors;   //scaling_available_governors :    this file shows the CPUfreq governors
  //                             available in this kernel. You can see the
  //                             currently activated governor in
  cpu_frequency_type governor;      //scaling_governor,     and by "echoing" the name of another
  //                      governor you can change it. Please note
  //                      that some governors won't load - they only
  //                      work on some specific architectures or
  //                      processors.

  //cpuinfo_cur_freq :        Current frequency of the CPU as obtained from
  //                the hardware, in KHz. This is the frequency
  //                the CPU actually runs at.

  std::vector<unsigned long> available_frequencies; //scaling_available_frequencies : List of available frequencies, in KHz.

  //scaling_min_freq and
  //scaling_max_freq        show the current "policy limits" (in
  //                kHz). By echoing new values into these
  //                files, you can change these limits.
  //                NOTE: when setting a policy you need to
  //                first set scaling_max_freq, then
  //                scaling_min_freq.

  std::string last_error;

  cpu_frequency(){} //Can't instantiate without a cpu number.

  bool get_number_from_file(std::string path, unsigned long& num);
  bool get_numbers_from_file(std::string path, std::vector<unsigned long>& nums);
  bool set_number_in_file(std::string path,unsigned long num);
  bool get_string_from_file(std::string path,std::string& str);
  bool get_strings_from_file(std::string path,std::vector<std::string>& strs);
  bool set_string_in_file(std::string path,std::string str);

public:

  cpu_frequency(int cpu_number);
  bool set_frequency(cpu_frequency_type type,unsigned long khz,unsigned long khzUpper,unsigned long khzLower);
  bool get_frequency(cpu_frequency_type& type,unsigned long& khz, unsigned long& khzUpper, unsigned long& khzLower);
  bool get_pstate_frequency(cpu_frequency_type pstate,unsigned long& khz);
  bool get_nearest_available_frequency(unsigned long reqKhz,unsigned long& actualKhz);
  bool is_governor_available(cpu_frequency_type governor);
  void get_governor_string(cpu_frequency_type type,std::string& str);
  cpu_frequency_type get_governor_type(std::string &freq_type);
  bool is_valid()
    {
    return valid;
    }
  const char *get_last_error()
    {
    return last_error.c_str();
    }
  };

#endif /* CPU_FREQUENCY_HPP */
