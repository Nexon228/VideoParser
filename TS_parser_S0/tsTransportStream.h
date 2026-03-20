#pragma once
#include "tsCommon.h"
#include <string>
#include <cstdio>
#include <cstring>

/*
MPEG-TS packet:
`        3                   2                   1                   0  `
`      1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0  `
`     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ `
`   0 |                             Header                            | `
`     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ `
`   4 |                  Adaptation field + Payload                   | `
`     |                                                               | `
` 184 |                                                               | `
`     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ `


MPEG-TS packet header:
`        3                   2                   1                   0  `
`      1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0  `
`     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ `
`   0 |       SB      |E|S|T|           PID           |TSC|AFC|   CC  | `
`     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ `

Sync byte                    (SB ) :  8 bits
Transport error indicator    (E  ) :  1 bit
Payload unit start indicator (S  ) :  1 bit
Transport priority           (T  ) :  1 bit
Packet Identifier            (PID) : 13 bits
Transport scrambling control (TSC) :  2 bits
Adaptation field control     (AFC) :  2 bits
Continuity counter           (CC ) :  4 bits
*/


//=============================================================================================================================================================================
class xTS
{
public:
  static constexpr uint32_t TS_PacketLength  = 188;
  static constexpr uint32_t TS_HeaderLength  = 4;

  static constexpr uint32_t PES_HeaderLength = 6;

  static constexpr uint32_t BaseClockFrequency_Hz         =    90000;
  static constexpr uint32_t ExtendedClockFrequency_Hz     = 27000000;
  static constexpr uint32_t BaseClockFrequency_kHz        =       90;
  static constexpr uint32_t ExtendedClockFrequency_kHz    =    27000;
  static constexpr uint32_t BaseToExtendedClockMultiplier =      300;
};

//=============================================================================================================================================================================

class xTS_PacketHeader
{
public:
  enum class ePID : uint16_t
  {
    PAT  = 0x0000,
    CAT  = 0x0001,
    TSDT = 0x0002,
    IPMT = 0x0003,
    NIT  = 0x0010,
    SDT  = 0x0011,
    NuLL = 0x1FFF,
  };

protected:
  // Pola 4-bajtowego nagłówka TS
  uint8_t  m_SB;   // Sync Byte
  uint8_t  m_E;    // Transport Error Indicator
  uint8_t  m_S;    // Payload Unit Start Indicator
  uint8_t  m_T;    // Transport Priority
  uint16_t m_PID;  // Packet Identifier
  uint8_t  m_TSC;  // Transport Scrambling Control
  uint8_t  m_AFC;  // Adaptation Field Control
  uint8_t  m_CC;   // Continuity Counter

public:
  void     Reset();
  int32_t  Parse(const uint8_t* Input);
  void     Print() const;

public:
  uint8_t  getSyncByte() const { return m_SB; }
  uint8_t  getTransportErrorIndicator() const { return m_E; }
  uint8_t  getPayloadUnitStartIndicator() const { return m_S; }
  uint8_t  getTransportPriority() const { return m_T; }
  uint16_t getPID() const { return m_PID; }
  uint8_t  getTransportScramblingControl() const { return m_TSC; }
  uint8_t  getAdaptationFieldControl() const { return m_AFC; }
  uint8_t  getContinuityCounter() const { return m_CC; }

public:
  bool     hasAdaptationField() const { return m_AFC == 2 || m_AFC == 3; }
  bool     hasPayload()         const { return m_AFC == 1 || m_AFC == 3; }
};

//=============================================================================================================================================================================

class xTS_AdaptationField
{
protected:
  uint8_t m_AdaptationFieldControl;
  uint8_t m_AdaptationFieldLength;

  uint8_t m_DC;  // discontinuity_indicator
  uint8_t m_RA;  // random_access_indicator
  uint8_t m_SP;  // elementary_stream_priority_indicator
  uint8_t m_PR;  // PCR_flag
  uint8_t m_OR;  // OPCR_flag
  uint8_t m_SF;  // splicing_point_flag
  uint8_t m_TP;  // transport_private_data_flag
  uint8_t m_EX;  // adaptation_field_extension_flag

public:
  void    Reset();
  int32_t Parse(const uint8_t* PacketBuffer, uint8_t AdaptationFieldControl);
  void    Print() const;

public:
  uint8_t getAdaptationFieldLength() const { return m_AdaptationFieldLength; }
  uint8_t getAdaptationFieldControl() const { return m_AdaptationFieldControl; }
};

//=============================================================================================================================================================================

class xPES_PacketHeader
{
public:
  enum eStreamId : uint8_t
  {
    eStreamId_program_stream_map       = 0xBC,
    eStreamId_padding_stream           = 0xBE,
    eStreamId_private_stream_2         = 0xBF,
    eStreamId_ECM                      = 0xF0,
    eStreamId_EMM                      = 0xF1,
    eStreamId_DSMCC_stream             = 0xF2,
    eStreamId_program_stream_directory = 0xFF,
    eStreamId_ITUT_H222_1_type_E       = 0xF8,
  };

protected:
  uint32_t m_PacketStartCodePrefix;
  uint8_t  m_StreamId;
  uint16_t m_PacketLength;

public:
  void    Reset();
  int32_t Parse(const uint8_t* Input);
  void    Print() const;

public:
  uint32_t getPacketStartCodePrefix() const { return m_PacketStartCodePrefix; }
  uint8_t  getStreamId() const { return m_StreamId; }
  uint16_t getPacketLength() const { return m_PacketLength; }
};

//=============================================================================================================================================================================

class xPES_Assembler
{
public:
  enum class eResult : int32_t
  {
    UnexpectedPID = 1,
    StreamPackedLost,
    AssemblingStarted,
    AssemblingContinue,
    AssemblingFinished,
  };

protected:
  int32_t  m_PID;

  uint8_t* m_Buffer;
  uint32_t m_BufferSize;
  uint32_t m_DataOffset;

  int8_t   m_LastContinuityCounter;
  bool     m_Started;

  xPES_PacketHeader m_PESH;

public:
  xPES_Assembler();
  ~xPES_Assembler();

  void Init(int32_t PID);

  eResult AbsorbPacket(const uint8_t* TransportStreamPacket,
                       const xTS_PacketHeader* PacketHeader,
                       const xTS_AdaptationField* AdaptationField);

  void PrintPESH() const { m_PESH.Print(); }

  uint8_t* getPacket() { return m_Buffer; }
  int32_t  getNumPacketBytes() const { return (int32_t)m_DataOffset; }

protected:
  void xBufferReset();
  void xBufferAppend(const uint8_t* Data, int32_t Size);
};