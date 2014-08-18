#include "stream.h"
#include <iostream>
#include <string.h>
#include <unistd.h>

using namespace std;

Stream::Stream(size_t hz, size_t channels, size_t bps) : hz_(hz), channels_(channels), bytesPerSample_(bps/8), sleep(0), median(0), shortMedian(0), lastUpdate(0), latencyMs(0)
{
	pBuffer = new DoubleBuffer<int>(1000);
	pShortBuffer = new DoubleBuffer<int>(200);
	pMiniBuffer = new DoubleBuffer<int>(20);
	pCardBuffer = new DoubleBuffer<int>(50);
	bufferMs = 500;
}



void Stream::setBufferLen(size_t bufferLenMs)
{
	bufferMs = bufferLenMs;
}



void Stream::addChunk(Chunk* chunk)
{
//	cout << "new chunk: " << chunk->getDuration() << "\n";
	while (chunks.size() * chunk->getDuration() > 10000)
		chunks.pop();
	chunks.push(shared_ptr<Chunk>(chunk));
}



void Stream::getSilentPlayerChunk(void* outputBuffer, unsigned long framesPerBuffer)
{
	memset(outputBuffer, 0, framesPerBuffer * channels_ * bytesPerSample_);//CHANNELS);
}


void Stream::setLatency(size_t latency)
{
	latencyMs = latency;
}


time_point_ms Stream::getNextPlayerChunk(void* outputBuffer, unsigned long framesPerBuffer, int correction)
{
	if (!chunk)
		chunk = chunks.pop();

	time_point_ms tp = chunk->timePoint();
	int read = 0;
	int toRead = framesPerBuffer*channels_ + correction*(hz_*channels_/1000);
	char* buffer;

	if (correction != 0)
	{
		int msBuffer = floor(framesPerBuffer*2 / (hz_*channels_/1000));
		if (abs(correction) > msBuffer / 2)
			correction = copysign(msBuffer / 2, correction);
		buffer = (char*)malloc(toRead * bytesPerSample_);
	}
	else
		buffer = (char*)outputBuffer;

	while (read < toRead)
	{
		read += chunk->read(buffer + read, toRead - read);
		if (chunk->isEndOfChunk())
			chunk = chunks.pop();
	}

	if (correction != 0)
	{
		float factor = (float)toRead / framesPerBuffer;//(float)(framesPerBuffer*channels_);
//float factor = 0.9;
//		std::cout << "correction: " << correction << ", factor: " << factor << "\n";
		float idx = 0;
		for (size_t n=0; n<framesPerBuffer; ++n)
		{
			size_t index(floor(idx));// = (int)(ceil(n*factor));
//cout << "toRead: " << toRead << ", n: " << n << ", idx: " << index << "\n";
			memcpy((char*)outputBuffer + n*chunk->frameSize_, buffer + index*chunk->frameSize_, chunk->frameSize_);
			idx += factor;
//			memcpy((char*)outputBuffer + n*bytesPerSample_*channels_, (char*)(chunk->wireChunk->payload) + index*bytesPerSample_*channels_, bytesPerSample_*channels_);
		}
		free(buffer);
/*		float factor = (float)toRead / (float)(framesPerBuffer*channels_);
		std::cout << "correction: " << correction << ", factor: " << factor << "\n";
		for (size_t n=0; n<framesPerBuffer; ++n)
		{
			size_t index = n;//(int)(floor(n*factor));
			memcpy((char*)outputBuffer + n*bytesPerSample_*channels_, (char*)(chunk->wireChunk->payload) + index*bytesPerSample_*channels_, bytesPerSample_*channels_);
//			memcpy((char*)outputBuffer + n*bytesPerSample_*channels_, (char*)(chunk->wireChunk->payload) + index*bytesPerSample_*channels_, bytesPerSample_*channels_);
		}
		free(buffer);
*/	}

	return tp;
}

/*
time_point_ms Stream::getNextPlayerChunk(void* outputBuffer, unsigned long framesPerBuffer, int correction)
{
correction = 0;
	if (!chunk)
		chunk = chunks.pop();

	time_point_ms tp = chunk->timePoint();
	int read = 0;
	int toRead = framesPerBuffer / 1.5;
	char* buffer;
//cout << "Framesize: " << chunk->frameSize_ << "\n";
	buffer = (char*)malloc(toRead * chunk->frameSize_);

	do
	{
		read += chunk->read(buffer + read, toRead - read);
		if (chunk->isEndOfChunk())
			chunk = chunks.pop();
	}
	while (read < toRead);

		float factor = (float)toRead / framesPerBuffer;//(float)(framesPerBuffer*channels_);
//float factor = 0.9;
//		std::cout << "correction: " << correction << ", factor: " << factor << "\n";
		float idx = 0;
		for (size_t n=0; n<framesPerBuffer; ++n)
		{
			size_t index(floor(idx));// = (int)(ceil(n*factor));
//cout << "toRead: " << toRead << ", n: " << n << ", idx: " << index << "\n";
			memcpy((char*)outputBuffer + n*chunk->frameSize_, buffer + index*chunk->frameSize_, chunk->frameSize_);
			idx += factor;
//			memcpy((char*)outputBuffer + n*bytesPerSample_*channels_, (char*)(chunk->wireChunk->payload) + index*bytesPerSample_*channels_, bytesPerSample_*channels_);
		}
		free(buffer);

	return tp;
}
*/

void Stream::updateBuffers(int age)
{
	pBuffer->add(age);
	pMiniBuffer->add(age);
	pShortBuffer->add(age);
}


void Stream::resetBuffers()
{
	pBuffer->clear();
	pMiniBuffer->clear();
	pShortBuffer->clear();
}


void Stream::getPlayerChunk(void* outputBuffer, double outputBufferDacTime, unsigned long framesPerBuffer)
{
//cout << "framesPerBuffer: " << framesPerBuffer << "\tms: " << framesPerBuffer*2 / PLAYER_CHUNK_MS_SIZE << "\t" << PLAYER_CHUNK_SIZE << "\n";
sleep = 0;
	int msBuffer = floor(framesPerBuffer*2 / (hz_*channels_/1000));

	int ticks = 0;
	long currentTick = getTickCount();
	if (lastTick == 0)
		lastTick = currentTick;
	ticks = currentTick - lastTick;
	lastTick = currentTick;

	int cardBuffer = 0;
	if (ticks > 1)
		pCardBuffer->add(ticks);
	if (pCardBuffer->full())
		cardBuffer = pCardBuffer->percentil(90);
		
	int correction = 0;
	if (sleep != 0)
	{
		resetBuffers();
		if (sleep < -10)
		{
//			std::cerr << "Sleep: " << sleep << "\n";
			sleep += msBuffer;
			if (sleep > -msBuffer/2)
				sleep = 0;
			getSilentPlayerChunk(outputBuffer, framesPerBuffer);
			return;
		}
		else if (sleep > 10)
		{
//			for (size_t i=0; i<chunks.size(); ++i)
//				std::cerr << "Chunk " << i << ": " << chunks[i]->getAge() - bufferMs << "\n";
			while (true)// (int i=0; i<(int)(round((float)sleep / (float)PLAYER_CHUNK_MS)) + 1; ++i)
			{
				int age = Chunk::getAge(getNextPlayerChunk(outputBuffer, framesPerBuffer)) - bufferMs + latencyMs;
				sleep -= msBuffer;
//		age += 4*cardBuffer;
//				cout << "age: " << age << ", msBuffer: " << msBuffer << "\n";
				if (sleep < msBuffer / 2)
					break;
			}
//			sleep = 0;
			return;
		}
		else if (sleep < 0)
		{	
			++sleep;
			correction = -1;
		}
		else if (sleep > 0)
		{	
			--sleep;
			correction = 1;
		}
	}
	


	int age = Chunk::getAge(getNextPlayerChunk(outputBuffer, framesPerBuffer, correction)) - bufferMs + latencyMs;// + outputBufferDacTime*1000;


	if (outputBufferDacTime < 1)
		age += outputBufferDacTime*1000;

//	if (pCardBuffer->full())
//		age += 4*cardBuffer;

//	cout << age << "\t" << framesPerBuffer << "\t" << msBuffer << "\t" << ticks << "\t" << cardBuffer << "\t" << outputBufferDacTime*1000 << "\n";


	if (sleep == 0)
	{
		if (pBuffer->full() && (abs(median) > 1))
		{
			cout << "pBuffer->full() && (abs(median) > 1): " << median << "\n";
			sleep = median;
		} 
		else if (pShortBuffer->full() && (abs(shortMedian) > 5))
		{
			cout << "pShortBuffer->full() && (abs(shortMedian) > 5): " << shortMedian << "\n";
			sleep = shortMedian;
		} 
		else if (pMiniBuffer->full() && (abs(pMiniBuffer->median()) > 50))
		{
//			cout << "pMiniBuffer->full() && (abs(pMiniBuffer->mean()) > 50): " << pMiniBuffer->median() << "\n";
			sleep = pMiniBuffer->mean();
		}
	}

	if (sleep != 0)
		std::cerr << "Sleep: " << sleep << "\n";
	

//	std::cerr << "Chunk: " << age << "\t" << outputBufferDacTime*1000 << "\n";
	if (ticks > 2)
	{
//		cout << age << "\n";
		updateBuffers(age);
	}
	time_t now = time(NULL);
	if (now != lastUpdate)
	{
		lastUpdate = now;
		median = pBuffer->median();
		shortMedian = pShortBuffer->median();
		std::cerr << "Chunk: " << age << "\t" << pMiniBuffer->median() << "\t" << shortMedian << "\t" << median << "\t" << pBuffer->size() << "\t" << cardBuffer << "\t" << outputBufferDacTime*1000 << "\n";
	}
}


