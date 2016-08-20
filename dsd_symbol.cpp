///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB.                                  //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <stdlib.h>
#include <assert.h>

#include "dsd_symbol.h"
#include "dsd_decoder.h"

namespace DSDcc
{

DSDSymbol::DSDSymbol(DSDDecoder *dsdDecoder) :
        m_dsdDecoder(dsdDecoder),
        m_symbol(0)
{
    resetSymbol();
    m_zeroCrossing = -1;
    m_zeroCrossingPos = 0;
    m_symCount1 = 0;
    m_umid = 0;
    m_lmid = 0;
    m_nbFSKSymbols = 2;
    m_zeroCrossingSlopeMin = 20000;
    m_invertedFSK = false;
    m_lastsample = 0;
    m_filteredSample = 0;
    m_numflips = 0;
    m_symbolSyncQuality = 0;
    m_symbolSyncQualityCounter = 0;
}

DSDSymbol::~DSDSymbol()
{
}

void DSDSymbol::noCarrier()
{
    m_zeroCrossing = -1;
    m_max = 15000;
    m_min = -15000;
    m_center = 0;
    m_filteredSample = 0;
}

void DSDSymbol::resetFrameSync()
{
    m_symCount1 = 0;
    m_lmin = 0;
    m_lmax = 0;
}

void DSDSymbol::resetSymbol()
{
    m_sampleIndex = 0;
    m_sum = 0;
    m_count = 0;
}

bool DSDSymbol::pushSample(short sample, bool have_sync)
{
    // short inSample = sample; // DEBUG

    // timing control

	if (m_sampleIndex == 0) // cycle start
	{
		if (m_zeroCrossingInCycle) // zero crossing established
		{
//            std::cerr << "DSDSymbol::pushSample: ZC adjust : " << m_zeroCrossing << std::endl;

            if (m_zeroCrossing < (m_dsdDecoder->m_state.samplesPerSymbol)/2) // sampling point lags
			{
			    m_zeroCrossingPos = -m_zeroCrossing;
				m_sampleIndex -= m_zeroCrossing / (m_dsdDecoder->m_state.samplesPerSymbol/4);
			}
			else // sampling point leads
			{
			    m_zeroCrossingPos = m_dsdDecoder->m_state.samplesPerSymbol - m_zeroCrossing;
				m_sampleIndex += (m_dsdDecoder->m_state.samplesPerSymbol - m_zeroCrossing)  / (m_dsdDecoder->m_state.samplesPerSymbol/4);
			}
		}

		m_zeroCrossingInCycle = false; // wait for next crossing
	}

    // process sample
    if (m_dsdDecoder->m_opts.use_cosine_filter)
    {
        if (m_dsdDecoder->m_state.samplesPerSymbol == 20) {
            sample = m_dsdFilters.nxdn_filter(sample); // 6.25 kHz for 2400 baud
        } else {
            sample = m_dsdFilters.dmr_filter(sample);  // 12.5 kHz for 4800 and 9600 baud
        }
    }

    m_filteredSample = sample;

    // zero crossing

    if (sample > m_center)
    {
        // transition edge with at least some slope
    	if ((m_lastsample < m_center) && ((sample - m_lastsample) > (m_zeroCrossingSlopeMin / m_dsdDecoder->m_state.samplesPerSymbol)))
    	{
            if (!m_zeroCrossingInCycle)
            {
                m_zeroCrossing = m_sampleIndex;
                m_numflips++;
                m_zeroCrossingInCycle = true;
            }
    	}
    }
    else
    {
        // transition edge with at least some slope
        if ((m_lastsample > m_center) && ((m_lastsample - sample) > (m_zeroCrossingSlopeMin / m_dsdDecoder->m_state.samplesPerSymbol)))
        {
            if (!m_zeroCrossingInCycle)
            {
                m_zeroCrossing = m_sampleIndex;
                m_numflips++;
                m_zeroCrossingInCycle = true;
            }
        }
    }

    // symbol estimation

    if (m_dsdDecoder->m_state.samplesPerSymbol == 5) // 9600 baud
    {
        if (m_sampleIndex == 2)
        {
            m_sum += sample;
            m_count++;
        }
    }
    else if (m_dsdDecoder->m_state.samplesPerSymbol == 20) // 2400 baud
    {
        if ((m_sampleIndex >= 5)
         && (m_sampleIndex <= 14))
        {
            m_sum += sample;
            m_count++;
        }
    }
    else // 4800 baud - default
    {
        if ((m_sampleIndex >= 3)
         && (m_sampleIndex <= 6))
        {
            m_sum += sample;
            m_count++;
        }
    }

    m_lastsample = sample;

    if (m_sampleIndex == m_dsdDecoder->m_state.samplesPerSymbol - 1) // conclusion
    {
        m_symbol = m_sum / m_count;
        m_dsdDecoder->m_state.symbolcnt++;

        // Symbol debugging
//        if ((m_dsdDecoder->m_state.symbolcnt > 1160) && (m_dsdDecoder->m_state.symbolcnt < 1200))  { // sampling
//            m_dsdDecoder->getLogger().log("DSDSymbol::pushSample: symbol %d (%d:%d) in:%d\n", m_dsdDecoder->m_state.symbolcnt, m_symbol, sample, inSample);
//        }

        resetSymbol();

        // moved here what was done at symbol retrieval in the decoder

        // symbol syncgronization quality metric

        if (m_symbolSyncQualityCounter < 100)
        {
            m_symbolSyncQualityCounter++;
        }
        else
        {
            m_symbolSyncQuality = m_numflips;
            m_symbolSyncQualityCounter = 0;
            m_numflips = 0;
        }

        // min/max calculation

        if (m_lidx < 32)
        {
            m_lidx++;
        }
        else
        {
            m_lidx = 0;
            snapLevels(32);
        }

        m_lbuf[m_lidx]    = m_symbol; // double buffering in case snap is required randomly
        m_lbuf[m_lidx+32] = m_symbol;

        return true; // new symbol available
    }
    else
    {
        m_sampleIndex++; // wait for next sample
        return false;
    }
}

void DSDSymbol::snapLevels(int nbSymbols)
{
    memcpy(m_lbuf2, &m_lbuf[32 + m_lidx - nbSymbols], nbSymbols * sizeof(int)); // copy to working buffer
    qsort(m_lbuf2, nbSymbols, sizeof(int), comp);

    m_lmin = (m_lbuf2[2] + m_lbuf2[3] + m_lbuf2[4]) / 3;
    m_lmax = (m_lbuf2[nbSymbols-3] + m_lbuf2[nbSymbols-4] + m_lbuf2[nbSymbols-5]) / 3;

    m_max = m_max + (m_lmax - m_max) / 4; // alpha = 0.25
    m_min = m_min + (m_lmin - m_min) / 4; // alpha = 0.25
    // recalibrate center/umid/lmid
    m_center = ((m_max) + (m_min)) / 2;
    m_umid = (((m_max) - m_center) / 2) + m_center;
    m_lmid = (((m_min) - m_center) / 2) + m_center;
}

void DSDSymbol::setFSK(unsigned int nbSymbols, bool inverted)
{
	if (nbSymbols == 2) // binary FSK a.k.a. 2FSK
	{
		m_nbFSKSymbols = 2;
		m_zeroCrossingSlopeMin = 20000;
	}
	else if (nbSymbols == 4) // 4-ary FSK a.k.a. 4FSK
	{
		m_nbFSKSymbols = 4;
		m_zeroCrossingSlopeMin = 40000;
	}
	else // others are not supported => default to binary FSK
	{
		m_nbFSKSymbols = 2;
        m_zeroCrossingSlopeMin = 20000;
	}

	m_invertedFSK = inverted;
}

int DSDSymbol::get_dibit_and_analog_signal(int* out_analog_signal)
{
    int symbol;
    int dibit;

    symbol = m_symbol;

    if (out_analog_signal != 0) {
        *out_analog_signal = symbol;
    }

    use_symbol(symbol);

    dibit = digitize(symbol);

    return dibit;
}

void DSDSymbol::use_symbol(int symbol)
{
    if (m_dsdDecoder->m_state.dibit_buf_p > m_dsdDecoder->m_state.dibit_buf + 900000)
    {
        m_dsdDecoder->m_state.dibit_buf_p = m_dsdDecoder->m_state.dibit_buf + 200;
    }
}

int DSDSymbol::digitize(int symbol)
{
    // determine dibit state

	if (m_nbFSKSymbols == 2)
	{
        if (symbol > m_center)
        {
            *m_dsdDecoder->m_state.dibit_buf_p = 1; // store non-inverted values in dibit_buf
            m_dsdDecoder->m_state.dibit_buf_p++;
            return (m_invertedFSK ? 1 : 0);
        }
        else
        {
            *m_dsdDecoder->m_state.dibit_buf_p = 3; // store non-inverted values in dibit_buf
            m_dsdDecoder->m_state.dibit_buf_p++;
            return (m_invertedFSK ? 0 : 1);
        }
	}
	else if (m_nbFSKSymbols == 4)
	{
		int dibit;

        if (symbol > m_center)
        {
            if (symbol > m_umid)
            {
                dibit = m_invertedFSK ? 3 : 1; // -3 / +3
                *m_dsdDecoder->m_state.dibit_buf_p = 1; // store non-inverted values in dibit_buf
            }
            else
            {
                dibit = m_invertedFSK ? 2 : 0; // -1 / +1
                *m_dsdDecoder->m_state.dibit_buf_p = 0; // store non-inverted values in dibit_buf
            }
        }
        else
        {
            if (symbol < m_lmid)
            {
                dibit = m_invertedFSK ? 1 : 3;  // +3 / -3
                *m_dsdDecoder->m_state.dibit_buf_p = 3; // store non-inverted values in dibit_buf
            }
            else
            {
                dibit = m_invertedFSK ? 0 : 2;  // +1 / -1
                *m_dsdDecoder->m_state.dibit_buf_p = 2; // store non-inverted values in dibit_buf
            }
        }

        m_dsdDecoder->m_state.dibit_buf_p++;
        return dibit;
	}
	else // invalid
	{
        *m_dsdDecoder->m_state.dibit_buf_p = 0;
        m_dsdDecoder->m_state.dibit_buf_p++;
		return 0;
	}
}

int DSDSymbol::invert_dibit(int dibit)
{
    switch (dibit)
    {
    case 0:
        return 2;
    case 1:
        return 3;
    case 2:
        return 0;
    case 3:
        return 1;
    }

    // Error, shouldn't be here
    assert(0);
    return -1;
}

int DSDSymbol::getDibit()
{
    return get_dibit_and_analog_signal(0);
}

int DSDSymbol::comp(const void *a, const void *b)
{
    if (*((const int *) a) == *((const int *) b))
        return 0;
    else if (*((const int *) a) < *((const int *) b))
        return -1;
    else
        return 1;
}


} // namespace DSDcc
