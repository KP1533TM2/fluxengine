#include "globals.h"
#include "decoders/decoders.h"
#include "mx/mx.h"
#include "crc.h"
#include "fluxmap.h"
#include "decoders/fluxmapreader.h"
#include "sector.h"
#include <string.h>

const int SECTOR_SIZE = 256;

/*
 * MX disks are a bunch of sectors glued together with no gaps or sync markers,
 * following a single beginning-of-track synchronisation and identification
 * sequence.
 */

/* FM beginning of track marker:
 * 1010 1010 1010 1010 1111 1111 1010 1111
 *    a    a    a    a    f    f    a    f
 */
const FluxPattern ID_PATTERN(32, 0xaaaaffaf);

class MxDecoder : public AbstractDecoder
{
public:
	MxDecoder(const DecoderProto& config):
		AbstractDecoder(config)
	{}

    void beginTrack()
	{
		_currentSector = -1;
		_bitcell = 0;
	}

    RecordType advanceToNextRecord()
	{
		if (_currentSector == -1)
		{
			/* First sector in the track: look for the sync marker. */
			const FluxMatcher* matcher = nullptr;
			_sector->bitcell = _bitcell = _fmr->seekToPattern(ID_PATTERN, matcher);
			_sector->clock = _sector->bitcell * 2;
			 readRawBits(32); /* skip the ID mark */
			_logicalTrack = decodeFmMfm(readRawBits(32)).slice(0, 32).reader().read_be16();
		}
		else if (_currentSector == 10)
		{
			/* That was the last sector on the disk. */
			return UNKNOWN_RECORD;
		}
		else
		{
			/* Otherwise we assume the clock from the first sector is still valid.
			 * The decoder framwork will automatically stop when we hit the end of
			 * the track. */
			_sector->bitcell = _bitcell;
		}

		_currentSector++;
		return SECTOR_RECORD;
	}

    void decodeSectorRecord()
	{
		auto bits = readRawBits((SECTOR_SIZE+2)*16);
		auto bytes = decodeFmMfm(bits).slice(0, SECTOR_SIZE+2).swab();

		uint16_t gotChecksum = 0;
		ByteReader br(bytes);
		for (int i=0; i<(SECTOR_SIZE/2); i++)
			gotChecksum += br.read_le16();
		uint16_t wantChecksum = br.read_le16();

		_sector->logicalTrack = _logicalTrack;
		_sector->logicalSide = _sector->physicalHead;
		_sector->logicalSector = _currentSector;
		_sector->data = bytes.slice(0, SECTOR_SIZE);
		_sector->status = (gotChecksum == wantChecksum) ? Sector::OK : Sector::BAD_CHECKSUM;
	}

private:
    nanoseconds_t _bitcell;
    int _currentSector;
    int _logicalTrack;
};

std::unique_ptr<AbstractDecoder> createMxDecoder(const DecoderProto& config)
{
	return std::unique_ptr<AbstractDecoder>(new MxDecoder(config));
}


