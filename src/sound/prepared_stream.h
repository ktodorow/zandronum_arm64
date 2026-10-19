#ifndef ZANDRONUM_PREPARED_STREAM_H
#define ZANDRONUM_PREPARED_STREAM_H

// Main prepares callbacks only for processed AL buffers. Implementations render
// owned PCM asynchronously; all AL calls and callback/control state stay main.
class PreparedSoundSource
{
public:
	virtual ~PreparedSoundSource() {}
	virtual bool Start(unsigned bytes) = 0;
	virtual void Stop() = 0;
	virtual bool UsesWorker() const { return false; }
	virtual void *Prepare(unsigned slot) = 0;
	virtual void Submit(unsigned slot, bool valid) = 0;
	virtual bool Collect(unsigned slot, const void *&pcm, bool &valid) = 0;
};

#endif
