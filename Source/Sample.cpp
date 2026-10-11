#include "Sample.h"
#include "DrumSynth.h"

namespace gm
{
void SampleData::buildPeaks (int bins)
{
    peaks.assign ((size_t) bins * 2, 0.0f);
    const int n = length();
    if (n == 0) return;
    for (int b = 0; b < bins; ++b)
    {
        const int a = (int) ((juce::int64) b * n / bins), e = std::max (a + 1, (int) ((juce::int64) (b + 1) * n / bins));
        float lo = 0, hi = 0;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* d = buffer.getReadPointer (c);
            for (int i = a; i < std::min (e, n); ++i)
            {
                lo = std::min (lo, d[i]);
                hi = std::max (hi, d[i]);
            }
        }
        peaks[(size_t) b * 2] = lo;
        peaks[(size_t) b * 2 + 1] = hi;
    }
}

SamplePool::SamplePool()
{
    formatManager.registerBasicFormats();
    for (auto& s : slots) s.store (nullptr);
    startTimer (1000);
}

SamplePool::~SamplePool()
{
    stopTimer();
}

bool SamplePool::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;opus;wv");
}

SampleData::Ptr SamplePool::load (const juce::File& f, juce::String& error, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr)
    {
        error = "Can't read " + f.getFileName();
        return nullptr;
    }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    if (len <= 0)
    {
        error = f.getFileName() + " is empty";
        return nullptr;
    }
    SampleData::Ptr s = new SampleData();
    const int ch = (int) std::min<unsigned int> (2, reader->numChannels);
    s->buffer.setSize (ch, len);
    reader->read (&s->buffer, 0, len, 0, true, ch > 1);
    s->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    s->file = f;
    s->name = f.getFileNameWithoutExtension();
    s->source = f.getFullPathName();
    s->buildPeaks();
    s->retiredAt = juce::Time::getMillisecondCounter(); // collectable until a slot adopts it

    const juce::ScopedLock sl (poolLock);
    pool.add (s);
    return s;
}

SampleData::Ptr SamplePool::loadSource (const juce::String& source, juce::String& error)
{
    if (! isSynth (source))
        return load (juce::File (source), error);
    SampleData::Ptr s = new SampleData();
    s->sampleRate = 48000.0;
    if (! drumsynth::render (source.fromFirstOccurrenceOf (":", false, false), s->sampleRate, s->buffer))
    {
        error = "unknown built-in sound " + source;
        return nullptr;
    }
    s->name = source.fromFirstOccurrenceOf (":", false, false);
    s->source = source;
    s->buildPeaks();
    s->retiredAt = juce::Time::getMillisecondCounter();
    const juce::ScopedLock sl (poolLock);
    pool.add (s);
    return s;
}

bool SamplePool::loadSourceIntoSlot (int track, const juce::String& source, juce::String& error)
{
    auto s = loadSource (source, error);
    if (s == nullptr) return false;
    setSlot (track, s);
    return true;
}

bool SamplePool::loadIntoSlot (int track, const juce::File& f, juce::String& error)
{
    auto s = load (f, error);
    if (s == nullptr) return false;
    setSlot (track, s);
    return true;
}

void SamplePool::setSlot (int track, SampleData::Ptr s)
{
    auto* old = slots[(size_t) track].exchange (s.get(), std::memory_order_acq_rel);
    if (old != nullptr) old->retiredAt = juce::Time::getMillisecondCounter();
    version.fetch_add (1);
}

void SamplePool::clearSlot (int track) { setSlot (track, nullptr); }

void SamplePool::timerCallback()
{
    const juce::ScopedLock sl (poolLock);
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = pool.size(); --i >= 0;)
    {
        SampleData* s = pool.getUnchecked (i).get();
        bool current = false;
        for (auto& sl2 : slots)
            current = current || sl2.load() == s;
        // refcount 1 = only the pool holds it: no voice is playing it.
        if (! current && s->getReferenceCount() == 1 && s->retiredAt != 0 && now - s->retiredAt > 3000)
            pool.remove (i);
    }
}
} // namespace gm
