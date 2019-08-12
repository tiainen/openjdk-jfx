/*
 * Copyright (C) 2004-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TextCodecUTF8.h"

#include "TextCodecASCIIFastPath.h"
#include <wtf/text/CString.h>
#include <wtf/text/StringBuffer.h>
#include <wtf/text/WTFString.h>
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {
using namespace WTF;
using namespace WTF::Unicode;

const int nonCharacter = -1;

void TextCodecUTF8::registerEncodingNames(EncodingNameRegistrar registrar)
{
    // From https://encoding.spec.whatwg.org.
    registrar("UTF-8", "UTF-8");
    registrar("utf8", "UTF-8");
    registrar("unicode-1-1-utf-8", "UTF-8");

    // Additional aliases that originally were present in the encoding
    // table in WebKit on Macintosh, and subsequently added by
    // TextCodecICU. Perhaps we can prove some are not used on the web
    // and remove them.
    registrar("unicode11utf8", "UTF-8");
    registrar("unicode20utf8", "UTF-8");
    registrar("x-unicode20utf8", "UTF-8");
}

void TextCodecUTF8::registerCodecs(TextCodecRegistrar registrar)
{
    registrar("UTF-8", [] {
        return std::make_unique<TextCodecUTF8>();
    });
}

static inline int nonASCIISequenceLength(uint8_t firstByte)
{
    static const uint8_t lengths[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    return lengths[firstByte];
}

static inline int decodeNonASCIISequence(const uint8_t* sequence, int& length)
{
    ASSERT(!isASCII(sequence[0]));
    if (length == 2) {
        ASSERT(sequence[0] >= 0xC2);
        ASSERT(sequence[0] <= 0xDF);
        if (sequence[1] < 0x80 || sequence[1] > 0xBF) {
            length = 1;
            return nonCharacter;
        }
        return ((sequence[0] << 6) + sequence[1]) - 0x00003080;
    }
    if (length == 3) {
        ASSERT(sequence[0] >= 0xE0);
        ASSERT(sequence[0] <= 0xEF);
        switch (sequence[0]) {
        case 0xE0:
            if (sequence[1] < 0xA0 || sequence[1] > 0xBF) {
                length = 1;
                return nonCharacter;
            }
            break;
        case 0xED:
            if (sequence[1] < 0x80 || sequence[1] > 0x9F) {
                length = 1;
                return nonCharacter;
            }
            break;
        default:
            if (sequence[1] < 0x80 || sequence[1] > 0xBF) {
                length = 1;
                return nonCharacter;
            }
        }
        if (sequence[2] < 0x80 || sequence[2] > 0xBF) {
            length = 2;
            return nonCharacter;
        }
        return ((sequence[0] << 12) + (sequence[1] << 6) + sequence[2]) - 0x000E2080;
    }
    ASSERT(length == 4);
    ASSERT(sequence[0] >= 0xF0);
    ASSERT(sequence[0] <= 0xF4);
    switch (sequence[0]) {
    case 0xF0:
        if (sequence[1] < 0x90 || sequence[1] > 0xBF) {
            length = 1;
            return nonCharacter;
        }
        break;
    case 0xF4:
        if (sequence[1] < 0x80 || sequence[1] > 0x8F) {
            length = 1;
            return nonCharacter;
        }
        break;
    default:
        if (sequence[1] < 0x80 || sequence[1] > 0xBF) {
            length = 1;
            return nonCharacter;
        }
    }
    if (sequence[2] < 0x80 || sequence[2] > 0xBF) {
        length = 2;
        return nonCharacter;
    }
    if (sequence[3] < 0x80 || sequence[3] > 0xBF) {
        length = 3;
        return nonCharacter;
    }
    return ((sequence[0] << 18) + (sequence[1] << 12) + (sequence[2] << 6) + sequence[3]) - 0x03C82080;
}

static inline UChar* appendCharacter(UChar* destination, int character)
{
    ASSERT(character != nonCharacter);
    ASSERT(!U_IS_SURROGATE(character));
    if (U_IS_BMP(character))
        *destination++ = character;
    else {
        *destination++ = U16_LEAD(character);
        *destination++ = U16_TRAIL(character);
    }
    return destination;
}

void TextCodecUTF8::consumePartialSequenceByte()
{
    --m_partialSequenceSize;
    memmove(m_partialSequence, m_partialSequence + 1, m_partialSequenceSize);
}

bool TextCodecUTF8::handlePartialSequence(LChar*& destination, const uint8_t*& source, const uint8_t* end, bool flush)
{
    ASSERT(m_partialSequenceSize);
    do {
        if (isASCII(m_partialSequence[0])) {
            *destination++ = m_partialSequence[0];
            consumePartialSequenceByte();
            continue;
        }
        int count = nonASCIISequenceLength(m_partialSequence[0]);
        if (!count)
            return true;

        if (count > m_partialSequenceSize) {
            if (count - m_partialSequenceSize > end - source) {
                if (!flush) {
                    // The new data is not enough to complete the sequence, so
                    // add it to the existing partial sequence.
                    memcpy(m_partialSequence + m_partialSequenceSize, source, end - source);
                    m_partialSequenceSize += end - source;
                    return false;
                }
                // An incomplete partial sequence at the end is an error, but it will create
                // a 16 bit string due to the replacementCharacter. Let the 16 bit path handle
                // the error.
                return true;
            }
            memcpy(m_partialSequence + m_partialSequenceSize, source, count - m_partialSequenceSize);
            source += count - m_partialSequenceSize;
            m_partialSequenceSize = count;
        }
        int character = decodeNonASCIISequence(m_partialSequence, count);
        if (character == nonCharacter || character > 0xFF)
            return true;

        m_partialSequenceSize -= count;
        *destination++ = character;
    } while (m_partialSequenceSize);

    return false;
}

void TextCodecUTF8::handlePartialSequence(UChar*& destination, const uint8_t*& source, const uint8_t* end, bool flush, bool stopOnError, bool& sawError)
{
    ASSERT(m_partialSequenceSize);
    do {
        if (isASCII(m_partialSequence[0])) {
            *destination++ = m_partialSequence[0];
            consumePartialSequenceByte();
            continue;
        }
        int count = nonASCIISequenceLength(m_partialSequence[0]);
        if (!count) {
            sawError = true;
            if (stopOnError)
                return;
            *destination++ = replacementCharacter;
            consumePartialSequenceByte();
            continue;
        }
        if (count > m_partialSequenceSize) {
            if (count - m_partialSequenceSize > end - source) {
                if (!flush) {
                    // The new data is not enough to complete the sequence, so
                    // add it to the existing partial sequence.
                    memcpy(m_partialSequence + m_partialSequenceSize, source, end - source);
                    m_partialSequenceSize += end - source;
                    return;
                }
                // An incomplete partial sequence at the end is an error.
                sawError = true;
                if (stopOnError)
                    return;
                *destination++ = replacementCharacter;
                m_partialSequenceSize = 0;
                source = end;
                continue;
            }
            memcpy(m_partialSequence + m_partialSequenceSize, source, count - m_partialSequenceSize);
            source += count - m_partialSequenceSize;
            m_partialSequenceSize = count;
        }
        int character = decodeNonASCIISequence(m_partialSequence, count);
        if (character == nonCharacter) {
            sawError = true;
            if (stopOnError)
                return;
            *destination++ = replacementCharacter;
            m_partialSequenceSize -= count;
            memmove(m_partialSequence, m_partialSequence + count, m_partialSequenceSize);
            continue;
        }

        m_partialSequenceSize -= count;
        destination = appendCharacter(destination, character);
    } while (m_partialSequenceSize);
}

String TextCodecUTF8::decode(const char* bytes, size_t length, bool flush, bool stopOnError, bool& sawError)
{
    // Each input byte might turn into a character.
    // That includes all bytes in the partial-sequence buffer because
    // each byte in an invalid sequence will turn into a replacement character.
    fprintf(stderr, "[JSY] 0\n");
    fprintf(stderr, "[JSY] 1; length = %d\n", length);
    StringBuffer<LChar> buffer(m_partialSequenceSize + length);

    const uint8_t* source = reinterpret_cast<const uint8_t*>(bytes);
    const uint8_t* end = source + length;
    const uint8_t* alignedEnd = WTF::alignToMachineWord(end);
    LChar* destination = buffer.characters();

    do {
        if (m_partialSequenceSize) {
            fprintf(stderr, "[JSY] 2\n");
            // Explicitly copy destination and source pointers to avoid taking pointers to the
            // local variables, which may harm code generation by disabling some optimizations
            // in some compilers.
            LChar* destinationForHandlePartialSequence = destination;
            const uint8_t* sourceForHandlePartialSequence = source;
            if (handlePartialSequence(destinationForHandlePartialSequence, sourceForHandlePartialSequence, end, flush)) {
                fprintf(stderr, "[JSY] 2a\n");
                source = sourceForHandlePartialSequence;
                goto upConvertTo16Bit;
            }
            destination = destinationForHandlePartialSequence;
            source = sourceForHandlePartialSequence;
            if (m_partialSequenceSize)
                fprintf(stderr, "[JSY] 2b\n");
                break;
        }

        fprintf(stderr, "[JSY] 3\n");
        while (source < end) {
            if (isASCII(*source)) {
                fprintf(stderr, "[JSY] 3a\n");
                // Fast path for ASCII. Most UTF-8 text will be ASCII.
                if (WTF::isAlignedToMachineWord(source)) {
                    fprintf(stderr, "[JSY] 3aa\n");
                    while (source < alignedEnd) {
                        auto chunk = *reinterpret_cast_ptr<const WTF::MachineWord*>(source);
                        if (!WTF::isAllASCII<LChar>(chunk))
                            break;
                        copyASCIIMachineWord(destination, source);
                        source += sizeof(WTF::MachineWord);
                        destination += sizeof(WTF::MachineWord);
                    }
                    fprintf(stderr, "[JSY] 3ab\n");
                    if (source == end)
                        break;
                    fprintf(stderr, "[JSY] 3ac\n");
                    if (!isASCII(*source))
                        continue;
                }
                fprintf(stderr, "[JSY] 3b\n");
                *destination++ = *source++;
                continue;
            }
            fprintf(stderr, "[JSY] 3c\n");
            int count = nonASCIISequenceLength(*source);
            int character;
            if (!count)
                character = nonCharacter;
            else {
                fprintf(stderr, "[JSY] 3d\n");
                if (count > end - source) {
                    fprintf(stderr, "[JSY] 3da\n");
                    ASSERT_WITH_SECURITY_IMPLICATION(end - source < static_cast<ptrdiff_t>(sizeof(m_partialSequence)));
                    ASSERT(!m_partialSequenceSize);
                    m_partialSequenceSize = end - source;
                    fprintf(stderr, "[JSY] 3db\n");
                    memcpy(m_partialSequence, source, m_partialSequenceSize);
                    fprintf(stderr, "[JSY] 3dc\n");
                    source = end;
                    break;
                }
                fprintf(stderr, "[JSY] 3e\n");
                character = decodeNonASCIISequence(source, count);
                fprintf(stderr, "[JSY] 3f\n");
            }
            fprintf(stderr, "[JSY] 3g\n");
            if (character == nonCharacter) {
                fprintf(stderr, "[JSY] 3h\n");
                sawError = true;
                if (stopOnError)
                    break;

                goto upConvertTo16Bit;
            }
            if (character > 0xFF) {
                fprintf(stderr, "[JSY] 3i\n");
                goto upConvertTo16Bit;
            }

            fprintf(stderr, "[JSY] 3j\n");
            source += count;
            fprintf(stderr, "[JSY] 3k\n");
            *destination++ = character;
        }
    } while (flush && m_partialSequenceSize);

    fprintf(stderr, "[JSY] 4\n");
    buffer.shrink(destination - buffer.characters());

    fprintf(stderr, "[JSY] 5\n");
    return String::adopt(WTFMove(buffer));

upConvertTo16Bit:
    StringBuffer<UChar> buffer16(m_partialSequenceSize + length);

    UChar* destination16 = buffer16.characters();

    fprintf(stderr, "[JSY] A\n");
    // Copy the already converted characters
    for (LChar* converted8 = buffer.characters(); converted8 < destination;)
        *destination16++ = *converted8++;

    fprintf(stderr, "[JSY] B\n");
    do {
        if (m_partialSequenceSize) {
            fprintf(stderr, "[JSY] C\n");
            // Explicitly copy destination and source pointers to avoid taking pointers to the
            // local variables, which may harm code generation by disabling some optimizations
            // in some compilers.
            UChar* destinationForHandlePartialSequence = destination16;
            const uint8_t* sourceForHandlePartialSequence = source;
            fprintf(stderr, "[JSY] D\n");
            handlePartialSequence(destinationForHandlePartialSequence, sourceForHandlePartialSequence, end, flush, stopOnError, sawError);
            fprintf(stderr, "[JSY] E\n");
            destination16 = destinationForHandlePartialSequence;
            source = sourceForHandlePartialSequence;
            if (m_partialSequenceSize)
                fprintf(stderr, "[JSY] F\n");
                break;
        }

        fprintf(stderr, "[JSY] G\n");
        while (source < end) {
            if (isASCII(*source)) {
                fprintf(stderr, "[JSY] Ga\n");
                // Fast path for ASCII. Most UTF-8 text will be ASCII.
                if (WTF::isAlignedToMachineWord(source)) {
                    fprintf(stderr, "[JSY] Gb\n");
                    while (source < alignedEnd) {
                        auto chunk = *reinterpret_cast_ptr<const WTF::MachineWord*>(source);
                        if (!WTF::isAllASCII<LChar>(chunk))
                            fprintf(stderr, "[JSY] Gc\n");
                            break;
                        copyASCIIMachineWord(destination16, source);
                        source += sizeof(WTF::MachineWord);
                        destination16 += sizeof(WTF::MachineWord);
                    }
                    fprintf(stderr, "[JSY] Gd\n");
                    if (source == end)
                        fprintf(stderr, "[JSY] Ge\n");
                        break;
                    fprintf(stderr, "[JSY] Gf\n");
                    if (!isASCII(*source))
                        fprintf(stderr, "[JSY] Gh\n");
                        continue;
                }
                fprintf(stderr, "[JSY] Gi\n");
                *destination16++ = *source++;
                continue;
            }
            fprintf(stderr, "[JSY] H\n");
            int count = nonASCIISequenceLength(*source);
            int character;
            if (!count)
                character = nonCharacter;
            else {
                fprintf(stderr, "[JSY] I\n");
                if (count > end - source) {
                    fprintf(stderr, "[JSY] I1\n");
                    ASSERT_WITH_SECURITY_IMPLICATION(end - source < static_cast<ptrdiff_t>(sizeof(m_partialSequence)));
                    fprintf(stderr, "[JSY] I2\n");
                    ASSERT(!m_partialSequenceSize);
                    fprintf(stderr, "[JSY] I3\n");
                    m_partialSequenceSize = end - source;
                    fprintf(stderr, "[JSY] I4\n");
                    memcpy(m_partialSequence, source, m_partialSequenceSize);
                    fprintf(stderr, "[JSY] I5\n");
                    source = end;
                    break;
                }
                fprintf(stderr, "[JSY] J\n");
                character = decodeNonASCIISequence(source, count);
                fprintf(stderr, "[JSY] K\n");
            }
            fprintf(stderr, "[JSY] L\n");
            if (character == nonCharacter) {
                fprintf(stderr, "[JSY] L1\n");
                sawError = true;
                if (stopOnError)
                    fprintf(stderr, "[JSY] L2\n");
                    break;
                *destination16++ = replacementCharacter;
                source += count ? count : 1;
                continue;
            }
            fprintf(stderr, "[JSY] M\n");
            source += count;
            fprintf(stderr, "[JSY] N\n");
            destination16 = appendCharacter(destination16, character);
        }
    } while (flush && m_partialSequenceSize);

    fprintf(stderr, "[JSY] O\n");
    buffer16.shrink(destination16 - buffer16.characters());

    fprintf(stderr, "[JSY] P\n");
    return String::adopt(WTFMove(buffer16));
}

Vector<uint8_t> TextCodecUTF8::encode(StringView string, UnencodableHandling)
{
    // The maximum number of UTF-8 bytes needed per UTF-16 code unit is 3.
    // BMP characters take only one UTF-16 code unit and can take up to 3 bytes (3x).
    // Non-BMP characters take two UTF-16 code units and can take up to 4 bytes (2x).
    Vector<uint8_t> bytes(WTF::checkedProduct<size_t>(string.length(), 3).unsafeGet());
    size_t bytesWritten = 0;
    for (auto character : string.codePoints())
        U8_APPEND_UNSAFE(bytes.data(), bytesWritten, character);
    bytes.shrink(bytesWritten);
    return bytes;
}

} // namespace WebCore
