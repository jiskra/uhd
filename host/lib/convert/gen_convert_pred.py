#!/usr/bin/env python
#
# Copyright 2011-2011 Ettus Research LLC
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

TMPL_TEXT = """
#import time
/***********************************************************************
 * This file was generated by $file on $time.strftime("%c")
 **********************************************************************/
typedef size_t pred_type;

\#include <boost/tokenizer.hpp>
\#include <boost/lexical_cast.hpp>
\#include <boost/detail/endian.hpp>
\#include <boost/cstdint.hpp>
\#include <stdexcept>
\#include <string>
\#include <vector>

enum dir_type{
    DIR_OTW_TO_CPU = 0,
    DIR_CPU_TO_OTW = 1
};

struct pred_error : std::runtime_error{
    pred_error(const std::string &what)
    :std::runtime_error("convert::make_pred: " + what){
        /* NOP */
    }
};

pred_type make_pred(const std::string &markup, dir_type &dir){
    pred_type pred = 0;

    try{
        boost::tokenizer<boost::char_separator<char> > tokenizer(markup, boost::char_separator<char>("_"));
        std::vector<std::string> tokens(tokenizer.begin(), tokenizer.end());
        //token 0 is <convert>
        std::string inp_type = tokens.at(1);
        std::string num_inps = tokens.at(2);
        //token 3 is <to>
        std::string out_type = tokens.at(4);
        std::string num_outs = tokens.at(5);
        std::string swap_type = tokens.at(6);

        std::string cpu_type, otw_type;
        if (inp_type.find("item") == std::string::npos){
            cpu_type = inp_type;
            otw_type = out_type;
            dir = DIR_CPU_TO_OTW;
        }
        else{
            cpu_type = out_type;
            otw_type = inp_type;
            dir = DIR_OTW_TO_CPU;
        }

        if      (cpu_type == "fc64") pred |= $ph.fc64_p;
        else if (cpu_type == "fc32") pred |= $ph.fc32_p;
        else if (cpu_type == "sc16") pred |= $ph.sc16_p;
        else if (cpu_type == "sc8")  pred |= $ph.sc8_p;
        else throw pred_error("unhandled io type " + cpu_type);

        if (otw_type == "item32") pred |= $ph.item32_p;
        else throw pred_error("unhandled otw type " + otw_type);

        int num_inputs = boost::lexical_cast<int>(num_inps);
        int num_outputs = boost::lexical_cast<int>(num_outs);

        switch(num_inputs*num_outputs){ //FIXME treated as one value
        case 1: pred |= $ph.chan1_p; break;
        case 2: pred |= $ph.chan2_p; break;
        case 3: pred |= $ph.chan3_p; break;
        case 4: pred |= $ph.chan4_p; break;
        default: throw pred_error("unhandled number of channels");
        }

        if      (swap_type == "bswap") pred |= $ph.bswap_p;
        else if (swap_type == "nswap") pred |= $ph.nswap_p;
        else throw pred_error("unhandled swap type");

    }
    catch(...){
        throw pred_error("could not parse markup: " + markup);
    }

    return pred;
}

UHD_INLINE pred_type make_pred(
    const io_type_t &io_type,
    const otw_type_t &otw_type,
    size_t num_inputs,
    size_t num_outputs
){
    pred_type pred = 0;

    switch(otw_type.byteorder){
    \#ifdef BOOST_BIG_ENDIAN
    case otw_type_t::BO_BIG_ENDIAN:    pred |= $ph.nswap_p; break;
    case otw_type_t::BO_LITTLE_ENDIAN: pred |= $ph.bswap_p; break;
    \#else
    case otw_type_t::BO_BIG_ENDIAN:    pred |= $ph.bswap_p; break;
    case otw_type_t::BO_LITTLE_ENDIAN: pred |= $ph.nswap_p; break;
    \#endif
    case otw_type_t::BO_NATIVE:        pred |= $ph.nswap_p; break;
    default: throw pred_error("unhandled otw byteorder type");
    }

    switch(otw_type.get_sample_size()){
    case sizeof(boost::uint32_t): pred |= $ph.item32_p; break;
    default: throw pred_error("unhandled otw sample size");
    }

    switch(io_type.tid){
    case io_type_t::COMPLEX_FLOAT32: pred |= $ph.fc32_p; break;
    case io_type_t::COMPLEX_INT16:   pred |= $ph.sc16_p; break;
    //case io_type_t::COMPLEX_INT8:    pred |= $ph.sc8_p; break;
    case io_type_t::COMPLEX_FLOAT64: pred |= $ph.fc64_p; break;
    default: throw pred_error("unhandled io type id");
    }

    switch(num_inputs*num_outputs){ //FIXME treated as one value
    case 1: pred |= $ph.chan1_p; break;
    case 2: pred |= $ph.chan2_p; break;
    case 3: pred |= $ph.chan3_p; break;
    case 4: pred |= $ph.chan4_p; break;
    default: throw pred_error("unhandled number of channels");
    }

    return pred;
}
"""

def parse_tmpl(_tmpl_text, **kwargs):
    from Cheetah.Template import Template
    return str(Template(_tmpl_text, kwargs))

class ph:
    bswap_p  = 0b00001
    nswap_p  = 0b00000
    item32_p = 0b00000
    sc8_p    = 0b00000
    sc16_p   = 0b00010
    fc32_p   = 0b00100
    fc64_p   = 0b00110
    chan1_p  = 0b00000
    chan2_p  = 0b01000
    chan3_p  = 0b10000
    chan4_p  = 0b11000

if __name__ == '__main__':
    import sys, os
    file = os.path.basename(__file__)
    open(sys.argv[1], 'w').write(parse_tmpl(TMPL_TEXT, file=file, ph=ph))