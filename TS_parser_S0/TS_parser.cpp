#include "tsCommon.h"
#include "tsTransportStream.h"
#include <cstdio>
#include <cstdlib>

//=============================================================================================================================================================================

static int32_t CalculatePESHeaderLength(const uint8_t* PESPacket)
{
  if (PESPacket == nullptr)
  {
    return NOT_VALID;
  }

  // Standardowy nagłówek PES:
  // 6 bajtów podstawowego headera
  // + 3 bajty części opcjonalnej
  // + PES_header_data_length z bajtu 8
  return 6 + 3 + PESPacket[8];
}

//=============================================================================================================================================================================

int main(int argc, char *argv[], char *envp[])
{
  (void)envp;

  // Sprawdzenie, czy podano nazwę pliku .ts
  if (argc < 2)
  {
    printf("Usage: %s input.ts\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Otwarcie pliku wejściowego w trybie binarnym
  FILE* TransportStreamFile = fopen(argv[1], "rb");
  if (TransportStreamFile == nullptr)
  {
    printf("Cannot open file: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  // Otwarcie pliku wyjściowego dla wyodrębnionego audio MP2
  FILE* OutputFile = fopen("PID136.mp2", "wb");
  if (OutputFile == nullptr)
  {
    printf("Cannot open output file: PID136.mp2\n");
    fclose(TransportStreamFile);
    return EXIT_FAILURE;
  }

  // Bufor na jeden pakiet MPEG-TS
  uint8_t TS_PacketBuffer[xTS::TS_PacketLength];

  // Obiekty parserów
  xTS_PacketHeader TS_PacketHeader;
  xTS_AdaptationField TS_PacketAdaptationField;
  xPES_PacketHeader PES_PacketHeader;
  xPES_Assembler PES_Assembler;

  // Inicjalizacja assemblera dla PID audio = 136
  PES_Assembler.Init(136);

  int32_t TS_PacketId = 0;

  // Czytamy plik pakiet po pakiecie, aż skończą się pełne 188-bajtowe bloki
  while (!feof(TransportStreamFile))
  {
    size_t NumRead = fread(TS_PacketBuffer, 1, xTS::TS_PacketLength, TransportStreamFile);
    if (NumRead != xTS::TS_PacketLength)
    {
      break;
    }

    // Parsowanie 4-bajtowego nagłówka TS
    TS_PacketHeader.Reset();
    TS_PacketHeader.Parse(TS_PacketBuffer);

    // Parsowanie adaptation field tylko wtedy, gdy pakiet go zawiera
    TS_PacketAdaptationField.Reset();
    if (TS_PacketHeader.hasAdaptationField())
    {
      TS_PacketAdaptationField.Parse(TS_PacketBuffer, TS_PacketHeader.getAdaptationFieldControl());
    }

    // Interesuje nas tylko PID 136, czyli strumień audio
    if (TS_PacketHeader.getPID() == 136)
    {
      printf("%010d ", TS_PacketId);
      TS_PacketHeader.Print();

      if (TS_PacketHeader.hasAdaptationField())
      {
        TS_PacketAdaptationField.Print();
      }

      // Dodatkowo dla pakietów rozpoczynających PES wypisujemy podstawowy nagłówek PES
      if (TS_PacketHeader.getPayloadUnitStartIndicator() == 1)
      {
        int32_t PayloadOffset = xTS::TS_HeaderLength;

        if (TS_PacketHeader.hasAdaptationField())
        {
          PayloadOffset += 1 + TS_PacketAdaptationField.getAdaptationFieldLength();
        }

        if (PayloadOffset + xTS::PES_HeaderLength <= xTS::TS_PacketLength)
        {
          PES_PacketHeader.Reset();
          PES_PacketHeader.Parse(TS_PacketBuffer + PayloadOffset);
          PES_PacketHeader.Print();
        }
      }

      // Przekazanie pakietu do assemblera PES
      xPES_Assembler::eResult Result =
        PES_Assembler.AbsorbPacket(TS_PacketBuffer, &TS_PacketHeader, &TS_PacketAdaptationField);

      switch (Result)
      {
        case xPES_Assembler::eResult::AssemblingStarted:
          printf(" Started");
          PES_Assembler.PrintPESH();
          break;

        case xPES_Assembler::eResult::AssemblingContinue:
          printf(" Continue");
          break;

        case xPES_Assembler::eResult::AssemblingFinished:
        {
          int32_t PESPacketLength = PES_Assembler.getNumPacketBytes();
          printf(" Finished PES: Len=%d", PESPacketLength);

          uint8_t* PESPacket = PES_Assembler.getPacket();
          if (PESPacket != nullptr && PESPacketLength > 9)
          {
            int32_t PESHeaderLength = CalculatePESHeaderLength(PESPacket);
            int32_t PESDataLength = PESPacketLength - PESHeaderLength;

            printf(" HeaderLen=%d DataLen=%d", PESHeaderLength, PESDataLength);

            // Zapisujemy tylko dane audio, bez nagłówka PES
            if (PESHeaderLength > 0 && PESDataLength > 0)
            {
              fwrite(PESPacket + PESHeaderLength, 1, (size_t)PESDataLength, OutputFile);
            }
          }

          // Ten sam pakiet z S=1 traktujemy jeszcze raz jako początek kolejnego PES
          PES_Assembler.Init(136);

          xPES_Assembler::eResult RestartResult =
            PES_Assembler.AbsorbPacket(TS_PacketBuffer, &TS_PacketHeader, &TS_PacketAdaptationField);

          if (RestartResult == xPES_Assembler::eResult::AssemblingStarted)
          {
            printf(" Restarted");
            PES_Assembler.PrintPESH();
          }

          break;
        }

        case xPES_Assembler::eResult::StreamPackedLost:
          printf(" PcktLost");
          break;

        default:
          break;
      }

      printf("\n");
    }

    TS_PacketId++;
  }

  fclose(OutputFile);
  fclose(TransportStreamFile);

  return EXIT_SUCCESS;
}

//=============================================================================================================================================================================