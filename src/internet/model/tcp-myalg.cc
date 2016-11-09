/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013 ResiliNets, ITTC, University of Kansas 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Siddharth Gangadhar <siddharth@ittc.ku.edu>,
 *          Truc Anh N. Nguyen <annguyen@ittc.ku.edu>,
 *          Greeshma Umapathi
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */

#include "tcp-myalg.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "rtt-estimator.h"
#include "tcp-socket-base.h"

NS_LOG_COMPONENT_DEFINE ("TcpMyAlg");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpMyAlg);

TypeId
TcpMyAlg::GetTypeId (void)
{
  static TypeId tid = TypeId("ns3::TcpMyAlg")
    .SetParent<TcpNewReno>()
    .SetGroupName ("Internet")
    .AddConstructor<TcpMyAlg>()
    .AddAttribute("FilterType", "Use this to choose no filter or Tustin's approximation filter",
                  EnumValue(TcpMyAlg::TUSTIN), MakeEnumAccessor(&TcpMyAlg::m_fType),
                  MakeEnumChecker(TcpMyAlg::NONE, "None", TcpMyAlg::TUSTIN, "Tustin"))
    .AddAttribute("ProtocolType", "Use this to let the code run as Westwood or WestwoodPlus",
                  EnumValue(TcpMyAlg::MYALG),
                  MakeEnumAccessor(&TcpMyAlg::m_pType),
                  MakeEnumChecker(TcpMyAlg::MYALG, "MyAlg",TcpMyAlg::MYALGPLUS, "MyAlgPlus"))
    .AddTraceSource("EstimatedBW", "The estimated bandwidth",
                    MakeTraceSourceAccessor(&TcpMyAlg::m_currentBW),
                    "ns3::TracedValueCallback::Double")
  ;
  return tid;
}

TcpMyAlg::TcpMyAlg (void) :
  TcpNewReno (),
  m_currentBW (0),
  m_lastSampleBW (0),
  m_lastBW (0),
  m_minRtt (Time (0)),
  m_ackedSegments (0),
  m_IsCount (false)
{
  NS_LOG_FUNCTION (this);
}

TcpMyAlg::TcpMyAlg (const TcpMyAlg& sock) :
  TcpNewReno (sock),
  m_currentBW (sock.m_currentBW),
  m_lastSampleBW (sock.m_lastSampleBW),
  m_lastBW (sock.m_lastBW),
  m_minRtt (Time (0)),
  m_pType (sock.m_pType),
  m_fType (sock.m_fType),
  m_IsCount (sock.m_IsCount)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
}

TcpMyAlg::~TcpMyAlg (void)
{
}

void
TcpMyAlg::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t packetsAcked,
                        const Time& rtt)
{
  NS_LOG_FUNCTION (this << tcb << packetsAcked << rtt);

  if (rtt.IsZero ())
    {
      NS_LOG_WARN ("RTT measured is zero!");
      return;
    }

  m_ackedSegments += packetsAcked;

  // Update minRtt
  if (m_minRtt.IsZero ())
    {
      m_minRtt = rtt;
    }
  else
    {
      if (rtt < m_minRtt)
        {
          m_minRtt = rtt;
        }
    }

  NS_LOG_LOGIC ("MinRtt: " << m_minRtt.GetMilliSeconds () << "ms");

  if (m_pType == TcpMyAlg::MYALG)
    {
      EstimateBW (rtt, tcb);
    }
  else if (m_pType == TcpMyAlg::MYALGPLUS)
    {
      if (!(rtt.IsZero () || m_IsCount))
        {
          m_IsCount = true;
          m_bwEstimateEvent.Cancel ();
          m_bwEstimateEvent = Simulator::Schedule (rtt, &TcpMyAlg::EstimateBW,
                                                   this, rtt, tcb);
        }
    }
}

void
TcpMyAlg::EstimateBW (const Time &rtt, Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (!rtt.IsZero ());

  m_currentBW = m_ackedSegments * tcb->m_segmentSize / rtt.GetSeconds ();

  if (m_pType == TcpMyAlg::MYALGPLUS)
    {
      m_IsCount = false;
    }

  m_ackedSegments = 0;
  NS_LOG_LOGIC ("Estimated BW: " << m_currentBW);

  // Filter the BW sample

  double alpha = 0.9;

  if (m_fType == TcpMyAlg::NONE)
    {
    }
  else if (m_fType == TcpMyAlg::TUSTIN)
    {
      double sample_bwe = m_currentBW;
      m_currentBW = (alpha * m_lastBW) + ((1 - alpha) * ((sample_bwe + m_lastSampleBW) / 2));
      m_lastSampleBW = sample_bwe;
      m_lastBW = m_currentBW;
    }

  NS_LOG_LOGIC ("Estimated BW after filtering: " << m_currentBW);
}

uint32_t
TcpMyAlg::GetSsThresh (Ptr<const TcpSocketState> tcb,
                          uint32_t bytesInFlight)
{
  (void) bytesInFlight;
  NS_LOG_LOGIC ("CurrentBW: " << m_currentBW << " minRtt: " <<
                m_minRtt << " ssthresh: " <<
                m_currentBW * static_cast<double> (m_minRtt.GetSeconds ()));

  return std::max (2*tcb->m_segmentSize,
                   uint32_t (m_currentBW * static_cast<double> (m_minRtt.GetSeconds ())));
}

Ptr<TcpCongestionOps>
TcpMyAlg::Fork ()
{
  return CreateObject<TcpMyAlg> (*this);
}

} // namespace ns3
