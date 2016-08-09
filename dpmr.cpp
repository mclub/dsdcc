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

#include <stdlib.h>
#include "dpmr.h"
#include "dsd_decoder.h"

namespace DSDcc
{

DSDdPMR::DSDdPMR(DSDDecoder *dsdDecoder) :
        m_dsdDecoder(dsdDecoder),
        m_state(DPMRHeader),
        m_symbolIndex(0),
        m_frameIndex(-1),
        m_colourCode(0)
{
}

DSDdPMR::~DSDdPMR()
{
}

void DSDdPMR::init()
{
    m_symbolIndex = 0;
    m_state = DPMRHeader;
}

void DSDdPMR::process() // just pass the frames for now
{
    switch(m_state)
    {
    case DPMRHeader:
        processHeader();
        break;
    case DPMRPostFrame:
        processPostFrame();
        break;
    case DPMRSuperFrame:
        processSuperFrame();
        break;
    case DPMREnd:
        processEndFrame();
        break;
    default:
        m_dsdDecoder->resetFrameSync(); // end
    };
}

void DSDdPMR::processHeader()
{
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get dibit from symbol

    if (m_symbolIndex == 0)
    {
        m_dsdDecoder->getLogger().log("DSDdPMR::processHeader: start\n"); // DEBUG
    }

    if (m_symbolIndex < 60) // HI0: TODO just pass for now
    {
        m_symbolIndex++;
    }
    else if (m_symbolIndex < 60 + 12) // Accumulate colour code di-bits
    {
        m_colourBuffer[m_symbolIndex - 72] = dibit;
        m_symbolIndex++;

        if (m_symbolIndex == 60 + 12) // colour code complete
        {
            m_colourBuffer[12] = '\0';
            processColourCode();
        }
    }
    else if (m_symbolIndex < 60 + 12 + 60) // HI1: TODO just pass for now
    {
        m_symbolIndex++;

        if (m_symbolIndex == 60 + 12 + 60) // header complete
        {
            m_state = DPMRPostFrame;
            m_symbolIndex = 0;
            m_frameIndex = -1;
        }
    }
    else // out of sync => terminate
    {
        m_dsdDecoder->resetFrameSync(); // end
    }
}

void DSDdPMR::processPostFrame()
{
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get di-bit from symbol

    if (m_symbolIndex == 0)
    {
        m_dsdDecoder->getLogger().log("DSDdPMR::processPostFrame: start\n"); // DEBUG
    }

    if (m_symbolIndex < 12) // look for a sync
    {
        if ((dibit == 0) || (dibit == 1)) // positives (+1 or +3) => store 1 which maps to +3
        {
            m_syncBuffer[m_symbolIndex] = '1';
        }
        else // negatives (-1 or -3) => store 3 which maps to -3
        {
            m_syncBuffer[m_symbolIndex] = '3';
        }

        m_symbolIndex++;

        if (m_symbolIndex == 12) // sync complete
        {
            m_syncBuffer[12] = '\0';
            m_dsdDecoder->getLogger().log("DSDdPMR::processPostFrame: sync: %s\n", m_syncBuffer); // DEBUG

            if (strcmp(m_syncBuffer, DPMR_FS2_SYNC) == 0) // start of superframes
            {
                m_state = DPMRSuperFrame;
                m_symbolIndex = 0;
            }
            else if (strcmp(m_syncBuffer, DPMR_FS3_SYNC) == 0) // end frame
            {
                m_state = DPMREnd;
                m_symbolIndex = 0;
            }
        }
    }
    else if (m_symbolIndex < 12 + 5*36) // length of a payload frame
    {
        m_symbolIndex++;
    }
    else
    {
        m_symbolIndex = 0; // back to FS2 or FS3 sync search
    }
}

void DSDdPMR::processSuperFrame()
{
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get di-bit from symbol

    if (m_symbolIndex < 36 + 144) // frame 0 or 2
    {
        if (m_symbolIndex == 0) // new frame
        {
            m_frameIndex++;
            m_dsdDecoder->getLogger().log("DSDdPMR::processSuperFrame: start even frame %d\n", m_frameIndex); // DEBUG
        }

        processEvenFrame();
        m_symbolIndex++;
    }
    else if (m_symbolIndex < 36 + 144 + 12) // frame 1 or 3 colour code
    {
        if (m_symbolIndex == 36 + 144) // new frame
        {
            m_frameIndex++;
            m_dsdDecoder->getLogger().log("DSDdPMR::processSuperFrame: start odd frame %d\n", m_frameIndex); // DEBUG
        }

        m_colourBuffer[m_symbolIndex - (36 + 144)] = dibit;
        m_symbolIndex++;

        if (m_symbolIndex == 36 + 144 + 12) // colour code complete
        {
            m_colourBuffer[12] = '\0';
            processColourCode();
        }
    }
    else if (m_symbolIndex < 36 + 144 + 12 + 36 + 144) // frame 1 or 3
    {
        processOddFrame();
        m_symbolIndex++;

        if (m_symbolIndex == 36 + 144 + 12 + 36 + 144) // frame complete
        {
            m_state = DPMRPostFrame; // check frame FS2 or FS3
            m_symbolIndex = 0;
        }
    }
    else // shouldn´t go there => out of sync error
    {
        m_dsdDecoder->resetFrameSync(); // end
    }
}

void DSDdPMR::processEvenFrame()
{
    // TODO
}

void DSDdPMR::processOddFrame()
{
    // TODO
}

void DSDdPMR::processEndFrame()
{
    if (m_symbolIndex == 0)
    {
        m_dsdDecoder->getLogger().log("DSDdPMR::processEndFrame: start\n"); // DEBUG
    }

    if (m_symbolIndex < 18) // END0: TODO: just pass for now
    {
        m_symbolIndex++;
    }
    else if (m_symbolIndex < 18) // END1: TODO: just pass for now
    {
        m_symbolIndex++;
    }
    else // terminated
    {
        m_dsdDecoder->resetFrameSync(); // end
    }
}

void DSDdPMR::processColourCode()
{
    m_colourCode = 0;

    for (int i = 11, n = 0; i >= 0; i--, n++) // colour code is stored MSB first
    {
        if ((m_colourBuffer[i] == 2) || (m_colourBuffer[i] == 3)) // -3 (11) => 1
        {
            m_colourCode += (1<<n); // bit is 1
        }
    }

    m_dsdDecoder->getLogger().log("DSDdPMR::processColourCode: %d\n", m_colourCode); // DEBUG
}

} // namespace DSDcc