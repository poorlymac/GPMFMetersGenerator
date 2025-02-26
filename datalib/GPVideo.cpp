/* GPVideo
 * 	Impersonates GoPro video
 *
 * 	Notez-bien : as of writing, every errors are fatal
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include "../date/include/date/date.h"

using namespace std;
using namespace date;
using namespace std::chrono;

extern "C" {
#include "../gpmf-parser/GPMF_parser.h"
#include "../gpmf-parser/GPMF_utils.h"
#include "../gpmf-parser/demo/GPMF_mp4reader.h"
}

#include "Context.h"
#include "GPVideo.h"

static const char *gpmfError( GPMF_ERROR err ){
	switch(err){
	case GPMF_OK : return "No problemo";
	case GPMF_ERROR_MEMORY : return "NULL Pointer or insufficient memory";
	case GPMF_ERROR_BAD_STRUCTURE : return "Non-complaint GPMF structure detected";
	case GPMF_ERROR_BUFFER_END : return "reached the end of the provided buffer";
	case GPMF_ERROR_FIND : return "Find failed to return the requested data, but structure is valid";
	case GPMF_ERROR_LAST : return "reached the end of a search at the current nest level";
	case GPMF_ERROR_TYPE_NOT_SUPPORTED : return "a needed TYPE tuple is missing or has unsupported elements";
	case GPMF_ERROR_SCALE_NOT_SUPPORTED : return "scaling for an non-scaling type, e.g. scaling a FourCC field to a float";
	case GPMF_ERROR_SCALE_COUNT : return "A SCAL tuple has a mismatching element count";
	case GPMF_ERROR_UNKNOWN_TYPE : return "Potentially valid data with a new or unknown type";
	case GPMF_ERROR_RESERVED : return "internal usage";
	}
	return "???";
}

void GPVideo::readGPMF( double cumul_dst ){
	uint32_t payloads = GetNumberPayloads(this->mp4handle);
	GPMF_stream metadata_stream, * ms = &metadata_stream;
	size_t payloadres = 0;
	time_t time = (time_t)-1;
	date::sys_time<std::chrono::milliseconds> tp;
	unsigned char gfix = 0;
	uint16_t dop = 1000;

	if(debug)
		printf("*d* payloads : %u\n", payloads);

	for(uint32_t index = 0; index < payloads; index++){
		GPMF_ERROR ret;
		double tstart, tend, tstep;	// Timeframe of this payload

		uint32_t payloadsize = GetPayloadSize(this->mp4handle, index);
		payloadres = GetPayloadResource(this->mp4handle, payloadres, payloadsize);
		uint32_t *payload = GetPayload(this->mp4handle, payloadres, index);

		if(debug)
			printf("*d* payload : %u ... ", index);

		if(!payload){	
			puts("*F* can't get Payload\n");
			exit(EXIT_FAILURE);
		}

		if((ret = (GPMF_ERROR)GetPayloadTime(this->mp4handle, index, &tstart, &tend)) != GPMF_OK){
			printf("*F* can't get payload's time : %s\n", gpmfError(ret));
			exit(EXIT_FAILURE);
		}

		if((ret = (GPMF_ERROR)GPMF_Init(ms, payload, payloadsize)) != GPMF_OK){
			printf("*F* can't init GPMF : %s\n", gpmfError(ret));
			exit(EXIT_FAILURE);
		}

		if(debug)
			printf("from %.3f to %.3f seconds\n", tstart, tend);

		while(GPMF_OK == GPMF_FindNext(ms, STR2FOURCC("STRM"), (GPMF_LEVELS)(GPMF_RECURSE_LEVELS|GPMF_TOLERANT) )){	// Looks for stream

			if(GPMF_OK == GPMF_FindNext(ms, STR2FOURCC("GPSF"), (GPMF_LEVELS)(GPMF_RECURSE_LEVELS|GPMF_TOLERANT) )){	// find out GPS fix if any
				if(GPMF_Type(ms) != GPMF_TYPE_UNSIGNED_LONG || GPMF_StructSize(ms) != 4){
					puts("*E* found GPSF which doesn't contain expected data");
					continue;
				}
				uint32_t *data = (uint32_t *)GPMF_RawData(ms);
				gfix = BYTESWAP32(*data);
				if (gfix != 3) {
					if(debug){
						printf("*E* ignoring poor fix %u\n", (unsigned)gfix);
					}
					continue;
				}
			}

			if(GPMF_OK == GPMF_FindNext(ms, STR2FOURCC("GPSU"), (GPMF_LEVELS)(GPMF_RECURSE_LEVELS|GPMF_TOLERANT) )){	// find out GPS time if any
				if(GPMF_Type(ms) != GPMF_TYPE_UTC_DATE_TIME){
					puts("*E* found GPSU which doesn't contain a date");
					continue;
				}

				if(GPMF_StructSize(ms) != 16){
					puts("*E* found GPSU but its structure size is not 16");
					continue;
				}

				const char *p = (const char *)GPMF_RawData(ms);
				if(debug){
					char t[17];
					t[16] = 0;

					strncpy(t, p, 16);
					printf("*I* GPSU : '%s'\n", t);
				}

				std::string oString;
				oString = p;
				istringstream in{oString};
				// Example is 240907235800.499GPSPS^B
				in >> parse("%y%m%d%H%M%6S", tp);
				if (in.fail()) {
					in.clear();
					printf("*E* GPSU : Date parse failure for '%s' ", p);
				}

				struct tm t;
				memset(&t, 0, sizeof(struct tm));
				t.tm_year = char2int(p) + 100;
				t.tm_mon = char2int(p += 2) - 1;
				t.tm_mday = char2int(p += 2);
				t.tm_hour = char2int(p += 2);
				t.tm_min = char2int(p += 2);
				t.tm_sec = char2int(p += 2);
				t.tm_isdst = -1;

					/* Convert to timestamp */
				time = mktime( &t );	// mktime() is based on local TZ
				time += t.tm_gmtoff;	// reverting back to Z (as it's GPS TZ)

				if(debug){
					printf("GPS time : %4d-%02d-%02d %02d:%02d:%02d (offset %6ld sec) -> ", 
						t.tm_year+1900, t.tm_mon+1, t.tm_mday,
						t.tm_hour, t.tm_min, t.tm_sec, t.tm_gmtoff
					);

					struct tm *tres = gmtime(&time);
					printtm(tres);
					puts("");
				}
			}

			if(GPMF_OK == GPMF_FindNext(ms, STR2FOURCC("GPSP"), (GPMF_LEVELS)(GPMF_RECURSE_LEVELS|GPMF_TOLERANT) )){	// find out GPS fix if any
				if(GPMF_Type(ms) != GPMF_TYPE_UNSIGNED_SHORT || GPMF_StructSize(ms) != 2){
					puts("*E* found GPSP which doesn't contain expected data");
					continue;
				}
				uint16_t *data = (unsigned short *)GPMF_RawData(ms);
				dop = BYTESWAP16(*data);

				if(enfquality && dop > 500){
					if(verbose)
						puts("*W* Sample ignored due to bad DoP");
					continue;
				}
			}

			if(GPMF_OK != GPMF_FindNext(ms, STR2FOURCC("GPS5"), (GPMF_LEVELS)(GPMF_RECURSE_LEVELS|GPMF_TOLERANT) ))	// No GPS data in this stream ... skipping
				continue;

			uint32_t samples = GPMF_Repeat(ms);
			uint32_t elements = GPMF_ElementsInStruct(ms);
			if(debug)
				printf("*d* %u samples, %u elements\n", samples, elements);

			if(elements != 5){
				printf("*E* Malformed GPMF : 5 elements experted by sample, got %d.\n", elements);
				continue;
			}

			if(samples){
				uint32_t type_samples = 1;

				uint32_t buffersize = samples * elements * sizeof(double);
				GPMF_stream find_stream;
				double *tmpbuffer = (double*)malloc(buffersize);

				uint32_t i;

				tstep = (tend - tstart)/samples;

				if(!tmpbuffer){
					puts("*F* Can't allocate temporary buffer\n");
					exit(EXIT_FAILURE);
				}

				GPMF_CopyState(ms, &find_stream);
				type_samples = 0;
				if(GPMF_OK == GPMF_FindPrev(&find_stream, GPMF_KEY_TYPE, (GPMF_LEVELS)(GPMF_CURRENT_LEVEL | GPMF_TOLERANT) ))
					type_samples = GPMF_Repeat(&find_stream);

				if(type_samples){
					printf("*E* Malformed GPMF : type_samples expected to be 0, got %d.\n", type_samples);
					continue;
				}

				if(
					GPMF_OK == GPMF_FindPrev(&find_stream, GPMF_KEY_SI_UNITS, (GPMF_LEVELS)(GPMF_CURRENT_LEVEL | GPMF_TOLERANT) ) ||
					GPMF_OK == GPMF_FindPrev(&find_stream, GPMF_KEY_UNITS, (GPMF_LEVELS)(GPMF_CURRENT_LEVEL | GPMF_TOLERANT) )
				){
					if(GPMF_OK == GPMF_ScaledData(ms, tmpbuffer, buffersize, 0, samples, GPMF_TYPE_DOUBLE)){	/* Output scaled data as floats */
						for(i = 0; i < samples; i++){
							double drift;

							if(debug)
								printf("t:%.3f l:%.3f l:%.3f a:%.3f 2d:%.3f 3d:%.3f gfix:%u dop:%u\n",
									tstart + i*tstep,
									tmpbuffer[i*elements + 0],	/* latitude */
									tmpbuffer[i*elements + 1],	/* longitude */
									tmpbuffer[i*elements + 2],	/* altitude */
									tmpbuffer[i*elements + 3],	/* speed2d */
									tmpbuffer[i*elements + 4],	/* speed3d */
									gfix, dop
								);

							if(!!(drift = this->addSample(
								tstart + i*tstep,
								tmpbuffer[i*elements + 0],	/* latitude */
								tmpbuffer[i*elements + 1],	/* longitude */
								tmpbuffer[i*elements + 2],	/* altitude */
								tmpbuffer[i*elements + 3],	/* speed2d */
								tmpbuffer[i*elements + 4],	/* speed3d */
								time,
								tp,
								gfix, dop,
								cumul_dst
							))){
								if(verbose)
									printf("*W* %.3f seconds : data drifting by %.3f\n", tstart + i*tstep, drift);
							}
						}
					}
				}
				free(tmpbuffer);
			}

		}
		GPMF_ResetState(ms);
	}

	if(payloadres)
		FreePayloadResource(mp4handle, payloadres);
	if(ms)
		GPMF_Free(ms);
}

double GPVideo::addSample( double sec, double lat, double lgt, double alt, double s2d, double s3d, time_t time,  date::sys_time<milliseconds> tp, unsigned char gfix, uint16_t dop, double cumul_dst ){
	double ret=0;

		/* Convert speed from m/s to km/h */
	s2d *= 3.6;
	s3d *= 3.6;

		/* update Min / Max */
	if(dop > this->dop)
		this->dop = dop;

	if(this->getSamples().empty()){	/* First data */
		this->getMin().set( lat, lgt, alt, time, tp );
		this->getMax().set( lat, lgt, alt, time, tp );
		this->getMin().spd2d = this->getMax().spd2d = s2d;
		this->getMin().spd3d = this->getMax().spd3d = s3d;
		this->getMin().gfix = this->getMax().gfix = gfix;
		this->getMin().dop = this->getMax().dop = dop;
	} else {
		if(lat < this->getMin().getLatitude())
			this->getMin().setLatitude(lat);
		if(lat > this->getMax().getLatitude())
			this->getMax().setLatitude(lat);

		if(lgt < this->getMin().getLongitude())
			this->getMin().setLongitude(lgt);
		if(lgt > this->getMax().getLongitude())
			this->getMax().setLongitude(lgt);

		if(alt < this->getMin().getAltitude())
			this->getMin().setAltitude(alt);
		if(alt > this->getMax().getAltitude())
			this->getMax().setAltitude(alt);

		if(s2d < this->getMin().spd2d)
			this->getMin().spd2d = s2d;
		if(s2d > this->getMax().spd2d)
			this->getMax().spd2d = s2d;

		if(s3d < this->getMin().spd3d)
			this->getMin().spd3d = s3d;
		if(s3d > this->getMax().spd3d)
			this->getMax().spd3d = s3d;

		if(time != (time_t)-1){
			if(this->getMin().getSampleTime() == (time_t)-1 || this->getMin().getSampleTime() > time)
				this->getMin().setSampleTime(time);
			if(this->getMax().getSampleTime() == (time_t)-1 || this->getMax().getSampleTime() < time)
				this->getMax().setSampleTime(time);
		}

		if(tp.time_since_epoch().count() != 0){
			if(this->getMin().getSampleTimeMS().time_since_epoch().count() == tp.time_since_epoch().count() || this->getMin().getSampleTimeMS().time_since_epoch().count() > tp.time_since_epoch().count())
				this->getMin().setSampleTimeMS(tp);
			if(this->getMax().getSampleTimeMS().time_since_epoch().count() == tp.time_since_epoch().count() || this->getMax().getSampleTimeMS().time_since_epoch().count() < tp.time_since_epoch().count())
				this->getMax().setSampleTimeMS(tp);
		}

		if(gfix < this->getMin().gfix)
			this->getMin().gfix = gfix;
		if(gfix > this->getMax().gfix)
			this->getMax().gfix = gfix;

		if(dop < this->getMin().dop)
			this->getMin().dop = dop;
		if(dop > this->getMax().dop)
			this->getMax().dop = dop;
	}

		/* manage multipart timing */
	this->lastTiming = sec;
	sec += this->voffset;	// shift by this part's offset

	if(this->getSamples().empty() || sec >= this->nextsample){	// store a new sample
		if(!this->getSamples().empty() && sec > this->nextsample + this->sample/2)	// Drifting
			ret = sec - (this->nextsample + this->sample/2);

		this->nextsample += this->sample;
		if(debug)
			printf("accepted : %f, next:%f\n", sec, this->nextsample);

		if(this->getSamples().empty()){	// 1st sample
			GPMFdata nv( lat, lgt, alt, s2d, s3d, time, tp, gfix, this->dop );
			nv.addDistance( cumul_dst );
			this->samples.push_back( nv );	// push into the list
		} else { // sample time
			GPMFdata nv(
				lat, lgt,
				this->calt/this->nbre, this->cs2d/this->nbre, this->cs3d/this->nbre, 
				time, tp, gfix, this->dop
			);
			nv.addDistance( this->getLast() );
			this->samples.push_back( nv );	// push into the list
		}

			/* reset pertinent fields */
		this->dop = 0;
		this->nbre = 1;
		this->calt = alt;
		this->cs2d = s2d;
		this->cs3d = s3d;
	} else {
		this->nbre++;
		this->calt += alt;
		this->cs2d += s2d;
		this->cs3d += s3d;
	}

	return ret;
}

GPVideo::GPVideo( char *fch, unsigned int asample, double cumul_dst ) : nextsample(0), voffset(0), dop(0) {
	this->sample = 1.0/asample;

	/* Ensure it's the 1st part of a GoPro video
	 *
	 * As GoPro's OpenMP4Source() needs a "char *",
	 * it's easier to manage this temporary file in C
	 * instead of a C++'s string
	 *
	 * https://community.gopro.com/s/article/GoPro-Camera-File-Naming-Convention
	 */
	char *fname = strdup(fch);
	assert(fname);		// quick & dirty : no raison to fail

		// "GX013561.MP4" -> 12 chars
	size_t len = strlen(fname);
	if(len < 12){
		fputs("*E* filename doesn't correspond to a GoPro video\n", stderr);
		exit(EXIT_FAILURE);
	}
	bool firstfile = true;
	if(strncmp(fname + len - 12, "GX01", 4) && strncmp(fname + len - 12, "GH01", 4)){
		printf("*L* not a GoPro named video, or not the 1st one\n");
		firstfile = false;
	}
	len -= 10;	// point to the part number


		/* Read the 1st chunck */
	printf("*L* Reading '%s'\n", fch);

	this->mp4handle = OpenMP4Source(fch, MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE, 0);
	if(!this->mp4handle){
		printf("*F* '%s' is an invalid MP4/MOV or it has no GPMF data\n\n", fch);
		exit(EXIT_FAILURE);
	}

	uint32_t frames = GetVideoFrameRateAndCount(this->mp4handle, &this->fr_num, &this->fr_dem);
	if(!frames){
		fputs("*F* Can't get frame count (incorrect MP4 ?)", stderr);
		exit(EXIT_FAILURE);
	}
	if(debug)
		printf("*I* Video framerate : %.3f (%u frames)\n", (float)this->fr_num / (float)this->fr_dem, frames);

	this->readGPMF( cumul_dst );

	CloseSource(this->mp4handle);
	this->mp4handle = 0;


		/* Check if there are others parts only if its the first file*/
 if(firstfile) {
	for(unsigned int i = 2; i<99; i++){	// As per GoPro, up to 98 parts
		char buff[3];
		sprintf(buff, "%02d", i);
		memcpy(fname + len, buff, 2);

		if(!access(fname, R_OK)){
			printf("*L* Adding '%s'\n", fname);
				
			this->AddPart(fname, cumul_dst);
		}
	}
 }
	
	free(fname);
}

void GPVideo::AddPart( char *fch, double cumul_dst ){
	this->voffset += this->lastTiming;

	this->mp4handle = OpenMP4Source(fch, MOV_GPMF_TRAK_TYPE, MOV_GPMF_TRAK_SUBTYPE, 0);
	if(!this->mp4handle){
		printf("*F* '%s' is an invalid MP4/MOV or it has no GPMF data\n\n", fch);
		exit(EXIT_FAILURE);
	}

	uint32_t fr_num, fr_dem;
	uint32_t frames = GetVideoFrameRateAndCount(this->mp4handle, &fr_num, &fr_dem);
	if(!frames){
		puts("*F* Can't get frame count (incorrect MP4 ?)");
		exit(EXIT_FAILURE);
	}
	if(debug)
		printf("*I* Video framerate : %.3f (%u frames)\n", (float)fr_num / (float)fr_dem, frames);

	if(fr_num != this->fr_num || fr_dem != this->fr_dem){
		puts("*F* Not the same frame rate");
		exit(EXIT_FAILURE);
	}

	this->readGPMF( cumul_dst );

	CloseSource(this->mp4handle);
	this->mp4handle = 0;
}

void GPVideo::Dump( void ){
	puts("*I* Video min/max :");
	printf("\tLatitude : %f deg - %f deg (%f)\n", this->getMin().getLatitude(), this->getMax().getLatitude(), this->getMax().getLatitude() - this->getMin().getLatitude());
	printf("\tLongitude : %f deg - %f deg (%f)\n", this->getMin().getLongitude(), this->getMax().getLongitude(), this->getMax().getLongitude() - this->getMin().getLongitude());
	printf("\tAltitude : %.3f m - %.3f m (%.3f)\n", this->getMin().getAltitude(), this->getMax().getAltitude(), this->getMax().getAltitude() - this->getMin().getAltitude());
	printf("\tSpeed2d : %.3f km/h - %.3f km/h (%.3f)\n", this->getMin().spd2d, this->getMax().spd2d, this->getMax().spd2d - this->getMin().spd2d);
	printf("\tSpeed3d : %.3f km/h - %.3f km/h (%.3f)\n", this->getMin().spd3d, this->getMax().spd3d, this->getMax().spd3d - this->getMin().spd3d);
	printf("\tgfix : %u -> %u, dop : %u -> %u\n", this->getMin().gfix, this->getMax().gfix, this->getMin().dop, this->getMax().dop);
	printf("\tDistance covered : %f m\n", this->getLast().getCumulativeDistance() );

	printf("\tTime : ");
	printtm(this->getMin().getGMT());
	printf(" -> ");
	printtm(this->getMax().getGMT());
	puts("");

	if(debug){
		puts("*D* Memorized video data");
    	for(auto p : samples){
			printf("\tTime : ");
			printtm(p.getGMT());
			puts("");

			printf("\t\tLatitude : %.3f deg\n", p.getLatitude());
			printf("\t\tLongitude : %.3f deg\n", p.getLongitude());
			printf("\t\tAltitude : %.3f m\n", p.getAltitude());
			printf("\t\tSpeed2d : %.3f km/h\n", p.spd2d);
			printf("\t\tSpeed3d : %.3f km/h\n", p.spd3d);
			printf("\t\tCumulative distance : %f m\n", p.getCumulativeDistance() );
			printf("\t\tgfix : %u, dop : %u\n", p.gfix, p.dop);
		}
	}

	printf("*I* %u memorised samples\n", this->getSampleCount());
}
