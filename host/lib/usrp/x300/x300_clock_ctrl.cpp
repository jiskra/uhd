//
// Copyright 2013-2014 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "lmk04816_regs.hpp"
#include "x300_clock_ctrl.hpp"
#include <uhd/utils/safe_call.hpp>
#include <boost/cstdint.hpp>
#include <boost/format.hpp>
#include <stdexcept>
#include <cmath>
#include <cstdlib>

static const double X300_REF_CLK_OUT_RATE  = 10e6;
static const boost::uint16_t X300_MAX_CLKOUT_DIV = 1045;
static const double X300_DEFAULT_DBOARD_CLK_RATE = 50e6;

using namespace uhd;

x300_clock_ctrl::~x300_clock_ctrl(void){
    /* NOP */
}

class x300_clock_ctrl_impl : public x300_clock_ctrl    {

public:

    ~x300_clock_ctrl_impl(void) {}

    x300_clock_ctrl_impl(uhd::spi_iface::sptr spiface,
        const size_t slaveno,
        const size_t hw_rev,
        const double master_clock_rate,
        const double system_ref_rate):
        _spiface(spiface),
        _slaveno(slaveno),
        _hw_rev(hw_rev),
        _master_clock_rate(master_clock_rate),
        _system_ref_rate(system_ref_rate)
    {
        init();
    }

    void reset_clocks() {
        _lmk04816_regs.RESET = lmk04816_regs_t::RESET_RESET;
        this->write_regs(0);
        _lmk04816_regs.RESET = lmk04816_regs_t::RESET_NO_RESET;
        for (size_t i = 0; i <= 16; ++i) {
            this->write_regs(i);
        }
        for (size_t i = 24; i <= 31; ++i) {
            this->write_regs(i);
        }
        sync_clocks();
    }

    void sync_clocks(void) {
        //soft sync:
        //put the sync IO into output mode - FPGA must be input
        //write low, then write high - this triggers a soft sync
        _lmk04816_regs.SYNC_POL_INV = lmk04816_regs_t::SYNC_POL_INV_SYNC_LOW;
        this->write_regs(11);
        _lmk04816_regs.SYNC_POL_INV = lmk04816_regs_t::SYNC_POL_INV_SYNC_HIGH;
        this->write_regs(11);
    }

    double get_master_clock_rate(void) {
        return _master_clock_rate;
    }

    double get_sysref_clock_rate(void) {
        return _system_ref_rate;
    }

    double get_refout_clock_rate(void) {
        //We support only one reference output rate
        return X300_REF_CLK_OUT_RATE;
    }

    void set_dboard_rate(const x300_clock_which_t which, double rate) {
        boost::uint16_t div = boost::uint16_t(_vco_freq / rate);
        boost::uint16_t *reg = NULL;
        boost::uint8_t addr = 0xFF;

        // Make sure requested rate is an even divisor of the VCO frequency
        if (not doubles_are_equal(_vco_freq / div, rate))
            throw uhd::value_error("invalid dboard rate requested");

        switch (which)
        {
        case X300_CLOCK_WHICH_DB0_RX:
        case X300_CLOCK_WHICH_DB1_RX:
            reg = &_lmk04816_regs.CLKout2_3_DIV;
            addr = 1;
            break;
        case X300_CLOCK_WHICH_DB0_TX:
        case X300_CLOCK_WHICH_DB1_TX:
            reg = &_lmk04816_regs.CLKout4_5_DIV;
            addr = 2;
            break;
        default:
            UHD_THROW_INVALID_CODE_PATH();
        }

        if (*reg == div)
            return;

        // Since the clock rate on one daughter board cannot be changed without
        // affecting the other daughter board, don't allow it.
        throw uhd::not_implemented_error("x3xx set dboard clock rate does not support changing the clock rate");

        // This is open source code and users may need to enable this function
        // to support other daughterboards.  If so, comment out the line above
        // that throws the error and allow the program to reach the code below.

        // The LMK04816 datasheet says the register must be written twice if SYNC is enabled
        *reg = div;
        write_regs(addr);
        write_regs(addr);
        sync_clocks();
    }

    double get_dboard_rate(const x300_clock_which_t which)
    {
        double rate = 0.0;
        switch (which)
        {
        case X300_CLOCK_WHICH_DB0_RX:
        case X300_CLOCK_WHICH_DB1_RX:
            rate = _vco_freq / _lmk04816_regs.CLKout2_3_DIV;
            break;
        case X300_CLOCK_WHICH_DB0_TX:
        case X300_CLOCK_WHICH_DB1_TX:
            rate = _vco_freq / _lmk04816_regs.CLKout4_5_DIV;
            break;
        default:
            UHD_THROW_INVALID_CODE_PATH();
        }
        return rate;
    }

    std::vector<double> get_dboard_rates(const x300_clock_which_t)
    {
        std::vector<double> rates;
        for (size_t div = size_t(_vco_freq / _master_clock_rate); div <= X300_MAX_CLKOUT_DIV; div++)
            rates.push_back(_vco_freq / div);
        return rates;
    }

    void enable_dboard_clock(const x300_clock_which_t which, const bool enable)
    {
        switch (which)
        {
        case X300_CLOCK_WHICH_DB0_RX:
            if (enable != (_lmk04816_regs.CLKout2_TYPE == lmk04816_regs_t::CLKOUT2_TYPE_LVPECL_700MVPP))
            {
                _lmk04816_regs.CLKout2_TYPE = enable ? lmk04816_regs_t::CLKOUT2_TYPE_LVPECL_700MVPP : lmk04816_regs_t::CLKOUT2_TYPE_P_DOWN;
                write_regs(6);
            }
            break;
        case X300_CLOCK_WHICH_DB1_RX:
            if (enable != (_lmk04816_regs.CLKout3_TYPE == lmk04816_regs_t::CLKOUT3_TYPE_LVPECL_700MVPP))
            {
                _lmk04816_regs.CLKout3_TYPE = enable ? lmk04816_regs_t::CLKOUT3_TYPE_LVPECL_700MVPP : lmk04816_regs_t::CLKOUT3_TYPE_P_DOWN;
                write_regs(6);
            }
            break;
        case X300_CLOCK_WHICH_DB0_TX:
            if (enable != (_lmk04816_regs.CLKout5_TYPE == lmk04816_regs_t::CLKOUT5_TYPE_LVPECL_700MVPP))
            {
                _lmk04816_regs.CLKout5_TYPE = enable ? lmk04816_regs_t::CLKOUT5_TYPE_LVPECL_700MVPP : lmk04816_regs_t::CLKOUT5_TYPE_P_DOWN;
                write_regs(7);
            }
            break;
        case X300_CLOCK_WHICH_DB1_TX:
            if (enable != (_lmk04816_regs.CLKout4_TYPE == lmk04816_regs_t::CLKOUT4_TYPE_LVPECL_700MVPP))
            {
                _lmk04816_regs.CLKout4_TYPE = enable ? lmk04816_regs_t::CLKOUT4_TYPE_LVPECL_700MVPP : lmk04816_regs_t::CLKOUT4_TYPE_P_DOWN;
                write_regs(7);
            }
            break;
        default:
            UHD_THROW_INVALID_CODE_PATH();
        }
    }

    void set_ref_out(const bool enable) {
        // TODO  Implement divider configuration to allow for configurable output
        // rates
        if (enable)
            _lmk04816_regs.CLKout10_TYPE = lmk04816_regs_t::CLKOUT10_TYPE_LVDS;
        else
            _lmk04816_regs.CLKout10_TYPE = lmk04816_regs_t::CLKOUT10_TYPE_P_DOWN;
        this->write_regs(8);
    }

    void write_regs(boost::uint8_t addr) {
        boost::uint32_t data = _lmk04816_regs.get_reg(addr);
        _spiface->write_spi(_slaveno, spi_config_t::EDGE_RISE, data,32);
    }


private:

    void init() {
        /* The X3xx has two primary rates. The first is the
         * _system_ref_rate, which is sourced from the "clock_source"/"value" field
         * of the property tree, and whose value can be 10e6, 30.72e6, or 200e6.
         * The _system_ref_rate is the input to the clocking system, and
         * what comes out is a disciplined master clock running at the
         * _master_clock_rate. As such, only certain combinations of
         * system reference rates and master clock rates are supported.
         * Additionally, a subset of these will operate in "zero delay" mode. */

        enum opmode_t { INVALID,
                        m10M_200M_NOZDEL,      // used for debug purposes only
                        m10M_200M_ZDEL,        // Normal mode
                        m30_72M_184_32M_ZDEL,  // LTE with external ref, aka CPRI Mode
                        m10M_184_32M_NOZDEL,   // LTE with 10 MHz ref
                        m10M_120M_ZDEL };       // NI USRP 120 MHz Clocking

        /* The default clocking mode is 10MHz reference generating a 200 MHz master
         * clock, in zero-delay mode. */
        opmode_t clocking_mode = INVALID;

        if(doubles_are_equal(_system_ref_rate, 10e6)) {
            if(doubles_are_equal(_master_clock_rate, 184.32e6)) {
                /* 10MHz reference, 184.32 MHz master clock out, NOT Zero Delay. */
                clocking_mode = m10M_184_32M_NOZDEL;
            } else if(doubles_are_equal(_master_clock_rate, 200e6)) {
                /* 10MHz reference, 200 MHz master clock out, Zero Delay */
                clocking_mode = m10M_200M_ZDEL;
            } else if(doubles_are_equal(_master_clock_rate, 120e6)) {
                /* 10MHz reference, 120 MHz master clock rate, Zero Delay */
                clocking_mode = m10M_120M_ZDEL;
            }
        } else if(doubles_are_equal(_system_ref_rate, 30.72e6)) {
            if(doubles_are_equal(_master_clock_rate, 184.32e6)) {
                /* 30.72MHz reference, 184.32 MHz master clock out, Zero Delay */
                clocking_mode = m30_72M_184_32M_ZDEL;
            }
        }

        if(clocking_mode == INVALID) {
            throw uhd::runtime_error(str(boost::format("A master clock rate of %f cannot be derived from a system reference rate of %f") % _master_clock_rate % _system_ref_rate));
        }

        // For 200 MHz output, the VCO is run at 2400 MHz
        // For the LTE/CPRI rate of 184.32 MHz, the VCO runs at 2580.48 MHz

        // Note: PLL2 N2 prescaler is enabled for all cases
        //       PLL2 reference doubler is enabled for all cases

        /* All LMK04816 settings are from the LMK datasheet for our clocking
         * architecture. Please refer to the datasheet for more information. */
        switch (clocking_mode) {
            case m10M_200M_NOZDEL:
                _vco_freq = 2400e6;
                _lmk04816_regs.MODE = lmk04816_regs_t::MODE_DUAL_INT;

                // PLL1 - 2 MHz compare frequency
                _lmk04816_regs.PLL1_N_28 = 48;
                _lmk04816_regs.PLL1_R_27 = 5;
                _lmk04816_regs.PLL1_CP_GAIN_27 = lmk04816_regs_t::PLL1_CP_GAIN_27_100UA;

                // PLL2 - 48 MHz compare frequency
                _lmk04816_regs.PLL2_N_30 = 25;
                _lmk04816_regs.PLL2_P_30 = lmk04816_regs_t::PLL2_P_30_DIV_2A;
                _lmk04816_regs.PLL2_R_28 = 4;
                _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_3200UA;

                break;

            case m10M_200M_ZDEL:
                _vco_freq = 2400e6;
                _lmk04816_regs.MODE = lmk04816_regs_t::MODE_DUAL_INT_ZER_DELAY;

                // PLL1 - 2 MHz compare frequency
                _lmk04816_regs.PLL1_N_28 = 5;
                _lmk04816_regs.PLL1_R_27 = 5;
                _lmk04816_regs.PLL1_CP_GAIN_27 = lmk04816_regs_t::PLL1_CP_GAIN_27_1600UA;

                // PLL2 - 96 MHz compare frequency
                _lmk04816_regs.PLL2_N_30 = 5;
                _lmk04816_regs.PLL2_P_30 = lmk04816_regs_t::PLL2_P_30_DIV_5;
                _lmk04816_regs.PLL2_R_28 = 2;

                if(_hw_rev <= 4)
                    _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_1600UA;
                else
                    _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_400UA;

                break;

            case m30_72M_184_32M_ZDEL:
                _vco_freq = 2580.48e6;
                _lmk04816_regs.MODE = lmk04816_regs_t::MODE_DUAL_INT_ZER_DELAY;

                // PLL1 - 2.048 MHz compare frequency
                _lmk04816_regs.PLL1_N_28 = 15;
                _lmk04816_regs.PLL1_R_27 = 15;
                _lmk04816_regs.PLL1_CP_GAIN_27 = lmk04816_regs_t::PLL1_CP_GAIN_27_100UA;

                // PLL2 - 7.68 MHz compare frequency
                _lmk04816_regs.PLL2_N_30 = 168;
                _lmk04816_regs.PLL2_P_30 = lmk04816_regs_t::PLL2_P_30_DIV_2A;
                _lmk04816_regs.PLL2_R_28 = 25;
                _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_3200UA;

                _lmk04816_regs.PLL2_R3_LF = lmk04816_regs_t::PLL2_R3_LF_1KILO_OHM;
                _lmk04816_regs.PLL2_C3_LF = lmk04816_regs_t::PLL2_C3_LF_39PF;

                _lmk04816_regs.PLL2_R4_LF = lmk04816_regs_t::PLL2_R4_LF_1KILO_OHM;
                _lmk04816_regs.PLL2_C4_LF = lmk04816_regs_t::PLL2_C4_LF_34PF;

                break;

            case m10M_184_32M_NOZDEL:
                _vco_freq = 2580.48e6;
                _lmk04816_regs.MODE = lmk04816_regs_t::MODE_DUAL_INT;

                // PLL1 - 2 MHz compare frequency
                _lmk04816_regs.PLL1_N_28 = 48;
                _lmk04816_regs.PLL1_R_27 = 5;
                _lmk04816_regs.PLL1_CP_GAIN_27 = lmk04816_regs_t::PLL1_CP_GAIN_27_100UA;

                // PLL2 - 7.68 MHz compare frequency
                _lmk04816_regs.PLL2_N_30 = 168;
                _lmk04816_regs.PLL2_P_30 = lmk04816_regs_t::PLL2_P_30_DIV_2A;
                _lmk04816_regs.PLL2_R_28 = 25;
                _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_3200UA;

                _lmk04816_regs.PLL2_R3_LF = lmk04816_regs_t::PLL2_R3_LF_4KILO_OHM;
                _lmk04816_regs.PLL2_C3_LF = lmk04816_regs_t::PLL2_C3_LF_39PF;

                _lmk04816_regs.PLL2_R4_LF = lmk04816_regs_t::PLL2_R4_LF_1KILO_OHM;
                _lmk04816_regs.PLL2_C4_LF = lmk04816_regs_t::PLL2_C4_LF_71PF;

                break;

            case m10M_120M_ZDEL:
                _vco_freq = 2400e6;
                _lmk04816_regs.MODE = lmk04816_regs_t::MODE_DUAL_INT_ZER_DELAY;

                // PLL1 - 2 MHz compare frequency
                _lmk04816_regs.PLL1_N_28 = 5;
                _lmk04816_regs.PLL1_R_27 = 5;
                _lmk04816_regs.PLL1_CP_GAIN_27 = lmk04816_regs_t::PLL1_CP_GAIN_27_100UA;

                // PLL2 - 96 MHz compare frequency
                _lmk04816_regs.PLL2_N_30 = 5;
                _lmk04816_regs.PLL2_P_30 = lmk04816_regs_t::PLL2_P_30_DIV_5;
                _lmk04816_regs.PLL2_R_28 = 2;

                if(_hw_rev <= 4)
                    _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_1600UA;
                else
                    _lmk04816_regs.PLL2_CP_GAIN_26 = lmk04816_regs_t::PLL2_CP_GAIN_26_400UA;

                break;

            default:
                UHD_THROW_INVALID_CODE_PATH();
                break;
        };

        boost::uint16_t master_clock_div = static_cast<boost::uint16_t>(
            std::ceil(_vco_freq / _master_clock_rate));

        boost::uint16_t dboard_div = static_cast<boost::uint16_t>(
            std::ceil(_vco_freq / X300_DEFAULT_DBOARD_CLK_RATE));

        /* Reset the LMK clock controller. */
        _lmk04816_regs.RESET = lmk04816_regs_t::RESET_RESET;
        this->write_regs(0);
        _lmk04816_regs.RESET = lmk04816_regs_t::RESET_NO_RESET;
        this->write_regs(0);

        /* Initial power-up */
        _lmk04816_regs.CLKout0_1_PD = lmk04816_regs_t::CLKOUT0_1_PD_POWER_UP;
        this->write_regs(0);
        _lmk04816_regs.CLKout0_1_DIV = master_clock_div;
        _lmk04816_regs.CLKout0_ADLY_SEL = lmk04816_regs_t::CLKOUT0_ADLY_SEL_D_EV_X;
        _lmk04816_regs.CLKout6_ADLY_SEL = lmk04816_regs_t::CLKOUT6_ADLY_SEL_D_BOTH;
        _lmk04816_regs.CLKout7_ADLY_SEL = lmk04816_regs_t::CLKOUT7_ADLY_SEL_D_BOTH;
        this->write_regs(0);

        // Register 1
        _lmk04816_regs.CLKout2_3_PD = lmk04816_regs_t::CLKOUT2_3_PD_POWER_UP;
        _lmk04816_regs.CLKout2_3_DIV = dboard_div;
        // Register 2
        _lmk04816_regs.CLKout4_5_PD = lmk04816_regs_t::CLKOUT4_5_PD_POWER_UP;
        _lmk04816_regs.CLKout4_5_DIV = dboard_div;
        // Register 3
        _lmk04816_regs.CLKout6_7_DIV = master_clock_div;
        _lmk04816_regs.CLKout6_7_OSCin_Sel = lmk04816_regs_t::CLKOUT6_7_OSCIN_SEL_VCO;
        // Register 4
        _lmk04816_regs.CLKout8_9_DIV = master_clock_div;
        // Register 5
        _lmk04816_regs.CLKout10_11_PD = lmk04816_regs_t::CLKOUT10_11_PD_NORMAL;
        _lmk04816_regs.CLKout10_11_DIV =
            static_cast<boost::uint16_t>(std::ceil(_vco_freq / _system_ref_rate));

        // Register 6
        _lmk04816_regs.CLKout0_TYPE = lmk04816_regs_t::CLKOUT0_TYPE_LVDS; //FPGA
        _lmk04816_regs.CLKout1_TYPE = lmk04816_regs_t::CLKOUT1_TYPE_P_DOWN; //CPRI feedback clock, use LVDS
        _lmk04816_regs.CLKout2_TYPE = lmk04816_regs_t::CLKOUT2_TYPE_LVPECL_700MVPP; //DB_0_RX
        _lmk04816_regs.CLKout3_TYPE = lmk04816_regs_t::CLKOUT3_TYPE_LVPECL_700MVPP; //DB_1_RX
        // Delay the FPGA_CLK by 900ps to ensure a safe ADC_SSCLK -> RADIO_CLK crossing.
        // If the FPGA_CLK is delayed, we also need to delay the reference clocks going to the DAC
        // because the data interface clock is generated from FPGA_CLK.
        // NOTE: This delay value was verified at room temperature only.
        _lmk04816_regs.CLKout0_1_ADLY = 0x10;

        // Register 7
        _lmk04816_regs.CLKout4_TYPE = lmk04816_regs_t::CLKOUT4_TYPE_LVPECL_700MVPP; //DB_1_TX
        _lmk04816_regs.CLKout5_TYPE = lmk04816_regs_t::CLKOUT5_TYPE_LVPECL_700MVPP; //DB_0_TX
        _lmk04816_regs.CLKout6_TYPE = lmk04816_regs_t::CLKOUT6_TYPE_LVPECL_700MVPP; //DB0_DAC
        _lmk04816_regs.CLKout7_TYPE = lmk04816_regs_t::CLKOUT7_TYPE_LVPECL_700MVPP; //DB1_DAC
        _lmk04816_regs.CLKout8_TYPE = lmk04816_regs_t::CLKOUT8_TYPE_LVPECL_700MVPP; //DB0_ADC
        _lmk04816_regs.CLKout6_7_ADLY = _lmk04816_regs.CLKout0_1_ADLY;

        // Register 8
        _lmk04816_regs.CLKout9_TYPE = lmk04816_regs_t::CLKOUT9_TYPE_LVPECL_700MVPP; //DB1_ADC
        _lmk04816_regs.CLKout10_TYPE = lmk04816_regs_t::CLKOUT10_TYPE_LVDS; //REF_CLKOUT
        _lmk04816_regs.CLKout11_TYPE = lmk04816_regs_t::CLKOUT11_TYPE_P_DOWN; //Debug header, use LVPECL


        // Register 10
        _lmk04816_regs.EN_OSCout0 = lmk04816_regs_t::EN_OSCOUT0_DISABLED; //Debug header
        _lmk04816_regs.FEEDBACK_MUX = 5; //use output 10 (REF OUT) for feedback
        _lmk04816_regs.EN_FEEDBACK_MUX = lmk04816_regs_t::EN_FEEDBACK_MUX_ENABLED;

        // Register 11
        // MODE set in individual cases above
        _lmk04816_regs.SYNC_QUAL = lmk04816_regs_t::SYNC_QUAL_FB_MUX;
        _lmk04816_regs.EN_SYNC = lmk04816_regs_t::EN_SYNC_ENABLE;
        _lmk04816_regs.NO_SYNC_CLKout0_1 = lmk04816_regs_t::NO_SYNC_CLKOUT0_1_CLOCK_XY_SYNC;
        _lmk04816_regs.NO_SYNC_CLKout2_3 = lmk04816_regs_t::NO_SYNC_CLKOUT2_3_CLOCK_XY_SYNC;
        _lmk04816_regs.NO_SYNC_CLKout4_5 = lmk04816_regs_t::NO_SYNC_CLKOUT4_5_CLOCK_XY_SYNC;
        _lmk04816_regs.NO_SYNC_CLKout6_7 = lmk04816_regs_t::NO_SYNC_CLKOUT6_7_CLOCK_XY_SYNC;
        _lmk04816_regs.NO_SYNC_CLKout8_9 = lmk04816_regs_t::NO_SYNC_CLKOUT8_9_CLOCK_XY_SYNC;
        _lmk04816_regs.NO_SYNC_CLKout10_11 = lmk04816_regs_t::NO_SYNC_CLKOUT10_11_CLOCK_XY_SYNC;
        _lmk04816_regs.SYNC_TYPE = lmk04816_regs_t::SYNC_TYPE_INPUT;

        // Register 12
        _lmk04816_regs.LD_MUX = lmk04816_regs_t::LD_MUX_BOTH;

        /* Input Clock Configurations */
        // Register 13
        _lmk04816_regs.EN_CLKin0 = lmk04816_regs_t::EN_CLKIN0_NO_VALID_USE;  // This is not connected
        _lmk04816_regs.EN_CLKin2 = lmk04816_regs_t::EN_CLKIN2_NO_VALID_USE;  // Used only for CPRI
        _lmk04816_regs.Status_CLKin1_MUX = lmk04816_regs_t::STATUS_CLKIN1_MUX_UWIRE_RB;
        _lmk04816_regs.CLKin_Select_MODE = lmk04816_regs_t::CLKIN_SELECT_MODE_CLKIN1_MAN;
        _lmk04816_regs.HOLDOVER_MUX = lmk04816_regs_t::HOLDOVER_MUX_PLL1_R;
        // Register 14
        _lmk04816_regs.Status_CLKin1_TYPE = lmk04816_regs_t::STATUS_CLKIN1_TYPE_OUT_PUSH_PULL;
        _lmk04816_regs.Status_CLKin0_TYPE = lmk04816_regs_t::STATUS_CLKIN0_TYPE_OUT_PUSH_PULL;

        // Register 26
        // PLL2_CP_GAIN_26 set above in individual cases
        _lmk04816_regs.PLL2_CP_POL_26 = lmk04816_regs_t::PLL2_CP_POL_26_NEG_SLOPE;
        _lmk04816_regs.EN_PLL2_REF_2X = lmk04816_regs_t::EN_PLL2_REF_2X_DOUBLED_FREQ_REF;

        // Register 27
        // PLL1_CP_GAIN_27 set in individual cases above
        // PLL1_R_27 set in the individual cases above

        // Register 28
        // PLL1_N_28 and PLL2_R_28 are set in the individual cases above

        // Register 29
        _lmk04816_regs.PLL2_N_CAL_29 = _lmk04816_regs.PLL2_N_30;  // N_CAL should always match N
        _lmk04816_regs.OSCin_FREQ_29 = lmk04816_regs_t::OSCIN_FREQ_29_63_TO_127MHZ;

        // Register 30
        // PLL2_P_30 set in individual cases above
        // PLL2_N_30 set in individual cases above

        /* Write the configuration values into the LMK */
        for (size_t i = 1; i <= 16; ++i) {
            this->write_regs(i);
        }
        for (size_t i = 24; i <= 31; ++i) {
            this->write_regs(i);
        }

        this->sync_clocks();
    }

    UHD_INLINE bool doubles_are_equal(double a, double b) {
        return  (std::fabs(a - b) < std::numeric_limits<double>::epsilon());
    }

    const spi_iface::sptr _spiface;
    const size_t _slaveno;
    const size_t _hw_rev;
    const double _master_clock_rate;
    const double _system_ref_rate;
    lmk04816_regs_t _lmk04816_regs;
    double _vco_freq;
};

x300_clock_ctrl::sptr x300_clock_ctrl::make(uhd::spi_iface::sptr spiface,
        const size_t slaveno,
        const size_t hw_rev,
        const double master_clock_rate,
        const double system_ref_rate) {
    return sptr(new x300_clock_ctrl_impl(spiface, slaveno, hw_rev,
                master_clock_rate, system_ref_rate));
}
