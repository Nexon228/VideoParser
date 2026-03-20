#include "tsTransportStream.h"
#include <cstdlib>

//=============================================================================================================================================================================
// xTS_PacketHeader
//=============================================================================================================================================================================

void xTS_PacketHeader::Reset()
{
  m_SB  = 0;
  m_E   = 0;
  m_S   = 0;
  m_T   = 0;
  m_PID = 0;
  m_TSC = 0;
  m_AFC = 0;
  m_CC  = 0;
}

int32_t xTS_PacketHeader::Parse(const uint8_t* Input)
{
  if (Input == nullptr)
  {
    return NOT_VALID;
  }

  // Bajt 0: sync byte, powinien mieć wartość 0x47 ('G')
  m_SB = Input[0];

  // Bajt 1: E, S, T oraz starsze 5 bitów PID
  m_E = (Input[1] >> 7) & 0x01;
  m_S = (Input[1] >> 6) & 0x01;
  m_T = (Input[1] >> 5) & 0x01;

  // PID ma 13 bitów: 5 młodszych bitów bajtu 1 i cały bajt 2
  m_PID = (uint16_t)(((Input[1] & 0x1F) << 8) | Input[2]);

  // Bajt 3: TSC, AFC, CC
  m_TSC = (Input[3] >> 6) & 0x03;
  m_AFC = (Input[3] >> 4) & 0x03;
  m_CC  =  Input[3]       & 0x0F;

  return xTS::TS_HeaderLength;
}

void xTS_PacketHeader::Print() const
{
  printf("TS: SB=%3d E=%d S=%d P=%d PID=%4d TSC=%d AF=%d CC=%2d",
         m_SB, m_E, m_S, m_T, m_PID, m_TSC, m_AFC, m_CC);
}

//=============================================================================================================================================================================
// xTS_AdaptationField
//=============================================================================================================================================================================

void xTS_AdaptationField::Reset()
{
  m_AdaptationFieldControl = 0;
  m_AdaptationFieldLength  = 0;

  m_DC = 0;
  m_RA = 0;
  m_SP = 0;
  m_PR = 0;
  m_OR = 0;
  m_SF = 0;
  m_TP = 0;
  m_EX = 0;
}

int32_t xTS_AdaptationField::Parse(const uint8_t* PacketBuffer, uint8_t AdaptationFieldControl)
{
  if (PacketBuffer == nullptr)
  {
    return NOT_VALID;
  }

  // Zapamiętanie wartości AFC z nagłówka TS
  m_AdaptationFieldControl = AdaptationFieldControl;

  // Adaptation field istnieje tylko dla AFC = 2 albo 3
  if (!(AdaptationFieldControl == 2 || AdaptationFieldControl == 3))
  {
    return 0;
  }

  // Bajt o indeksie 4 zawiera adaptation_field_length
  m_AdaptationFieldLength = PacketBuffer[4];

  // Jeśli długość wynosi 0, to AF kończy się na samym polu długości
  if (m_AdaptationFieldLength == 0)
  {
    return 1;
  }

  // Bajt o indeksie 5 zawiera zestaw flag adaptation field
  uint8_t Flags = PacketBuffer[5];

  m_DC = (Flags >> 7) & 0x01;
  m_RA = (Flags >> 6) & 0x01;
  m_SP = (Flags >> 5) & 0x01;
  m_PR = (Flags >> 4) & 0x01;
  m_OR = (Flags >> 3) & 0x01;
  m_SF = (Flags >> 2) & 0x01;
  m_TP = (Flags >> 1) & 0x01;
  m_EX =  Flags       & 0x01;

  // Całkowity rozmiar AF w pakiecie TS to:
  // 1 bajt długości + m_AdaptationFieldLength bajtów danych
  return 1 + m_AdaptationFieldLength;
}

void xTS_AdaptationField::Print() const
{
  printf(" AF: L=%2d DC=%d RA=%d SP=%d PR=%d OR=%d SF=%d TP=%d EX=%d",
         m_AdaptationFieldLength, m_DC, m_RA, m_SP, m_PR, m_OR, m_SF, m_TP, m_EX);
}

//=============================================================================================================================================================================
// xPES_PacketHeader
//=============================================================================================================================================================================

void xPES_PacketHeader::Reset()
{
  m_PacketStartCodePrefix = 0;
  m_StreamId = 0;
  m_PacketLength = 0;
}

int32_t xPES_PacketHeader::Parse(const uint8_t* Input)
{
  if (Input == nullptr)
  {
    return NOT_VALID;
  }

  // 24-bitowy start code prefix PES powinien mieć wartość 0x000001
  m_PacketStartCodePrefix = ((uint32_t)Input[0] << 16) |
                            ((uint32_t)Input[1] << 8)  |
                            ((uint32_t)Input[2]);

  // Identyfikator strumienia, np. audio
  m_StreamId = Input[3];

  // Długość pakietu PES zapisana w dwóch bajtach
  m_PacketLength = (uint16_t)(((uint16_t)Input[4] << 8) | Input[5]);

  return xTS::PES_HeaderLength;
}

void xPES_PacketHeader::Print() const
{
  printf(" PES: PSCP=%u SID=%u L=%u",
         m_PacketStartCodePrefix, m_StreamId, m_PacketLength);
}

//=============================================================================================================================================================================
// xPES_Assembler
//=============================================================================================================================================================================

xPES_Assembler::xPES_Assembler()
{
  m_PID = -1;
  m_Buffer = nullptr;
  m_BufferSize = 0;
  m_DataOffset = 0;
  m_LastContinuityCounter = -1;
  m_Started = false;
}

xPES_Assembler::~xPES_Assembler()
{
  delete[] m_Buffer;
  m_Buffer = nullptr;
}

void xPES_Assembler::Init(int32_t PID)
{
  // Zapamiętanie PID, który assembler ma śledzić
  m_PID = PID;

  // Bufor na składany PES
  m_BufferSize = 1024 * 1024;
  m_Buffer = new uint8_t[m_BufferSize];

  m_DataOffset = 0;
  m_LastContinuityCounter = -1;
  m_Started = false;
}

void xPES_Assembler::xBufferReset()
{
  // Zerowanie logicznej długości złożonego PES
  m_DataOffset = 0;
}

void xPES_Assembler::xBufferAppend(const uint8_t* Data, int32_t Size)
{
  if (Data == nullptr || Size <= 0)
  {
    return;
  }

  // Ochrona przed wyjściem poza zaalokowany bufor
  if (m_DataOffset + (uint32_t)Size > m_BufferSize)
  {
    return;
  }

  memcpy(m_Buffer + m_DataOffset, Data, (size_t)Size);
  m_DataOffset += (uint32_t)Size;
}

xPES_Assembler::eResult xPES_Assembler::AbsorbPacket(const uint8_t* TransportStreamPacket,
                                                     const xTS_PacketHeader* PacketHeader,
                                                     const xTS_AdaptationField* AdaptationField)
{
  if (TransportStreamPacket == nullptr || PacketHeader == nullptr || AdaptationField == nullptr)
  {
    return eResult::UnexpectedPID;
  }

  // Interesuje nas tylko PID ustawiony w Init()
  if (PacketHeader->getPID() != m_PID)
  {
    return eResult::UnexpectedPID;
  }

  // Jeżeli pakiet nie ma payloadu, nie ma czego doklejać do PES
  if (!PacketHeader->hasPayload())
  {
    return eResult::AssemblingContinue;
  }

  // Wyznaczenie początku payloadu w pakiecie TS:
  // 4 bajty nagłówka TS + opcjonalnie adaptation field
  int32_t PayloadOffset = xTS::TS_HeaderLength;
  if (PacketHeader->hasAdaptationField())
  {
    PayloadOffset += 1 + AdaptationField->getAdaptationFieldLength();
  }

  if (PayloadOffset >= (int32_t)xTS::TS_PacketLength)
  {
    return eResult::StreamPackedLost;
  }

  const uint8_t* PayloadPtr = TransportStreamPacket + PayloadOffset;
  int32_t PayloadSize = (int32_t)xTS::TS_PacketLength - PayloadOffset;

  // Jeśli S=1, payload zaczyna nowy PES
  if (PacketHeader->getPayloadUnitStartIndicator() == 1)
  {
    // Jeżeli wcześniej nic nie składaliśmy, startujemy nowy PES
    if (!m_Started)
    {
      xBufferReset();
      xBufferAppend(PayloadPtr, PayloadSize);

      m_PESH.Reset();
      if (m_DataOffset >= xTS::PES_HeaderLength)
      {
        m_PESH.Parse(m_Buffer);
      }

      m_LastContinuityCounter = (int8_t)PacketHeader->getContinuityCounter();
      m_Started = true;
      return eResult::AssemblingStarted;
    }

    // Jeśli już coś składaliśmy, to nowy S=1 oznacza koniec poprzedniego PES.
    // Nie zaczynamy jeszcze nowego w tym samym wywołaniu — to uproszczona wersja
    // do testów stanów assemblera.
    return eResult::AssemblingFinished;
  }

  // Jeśli nie było jeszcze startu PES, ignorujemy pakiety kontynuacyjne
  if (!m_Started)
  {
    return eResult::AssemblingContinue;
  }

  // Sprawdzenie continuity counter dla kolejnych pakietów tego samego PES
  int32_t ExpectedCC = (m_LastContinuityCounter + 1) & 0x0F;
  if (PacketHeader->getContinuityCounter() != ExpectedCC)
  {
    m_Started = false;
    xBufferReset();
    m_LastContinuityCounter = -1;
    return eResult::StreamPackedLost;
  }

  // Doklejenie payloadu kolejnego pakietu do aktualnie składanego PES
  xBufferAppend(PayloadPtr, PayloadSize);
  m_LastContinuityCounter = (int8_t)PacketHeader->getContinuityCounter();

  return eResult::AssemblingContinue;
}