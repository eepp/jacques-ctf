/*
 * Copyright (C) 2018 Philippe Proulx <eepp.ca> - All Rights Reserved
 *
 * Unauthorized copying of this file, via any medium, is strictly
 * prohibited. Proprietary and confidential.
 */

#ifndef _JACQUES_PACKET_INDEX_ENTRY_HPP
#define _JACQUES_PACKET_INDEX_ENTRY_HPP

#include <boost/optional.hpp>
#include <boost/operators.hpp>
#include <yactfr/metadata/fwd.hpp>

#include "aliases.hpp"
#include "timestamp.hpp"
#include "data-size.hpp"

namespace jacques {

class PacketIndexEntry :
    public boost::totally_ordered<PacketIndexEntry>
{
public:
    explicit PacketIndexEntry(Index indexInDataStream,
                              Index offsetInDataStreamBytes,
                              boost::optional<Index> packetContextOffsetInPacketBits,
                              const boost::optional<DataSize>& preambleSize,
                              const boost::optional<DataSize>& expectedTotalSize,
                              const boost::optional<DataSize>& expectedContentSize,
                              const DataSize& effectiveTotalSize,
                              const DataSize& effectiveContentSize,
                              const yactfr::DataStreamType *dst,
                              boost::optional<Index> dataStreamId,
                              boost::optional<Timestamp> tsBegin,
                              boost::optional<Timestamp> tsEnd,
                              boost::optional<Index> seqNum,
                              boost::optional<Size> discardedEventRecordCounter,
                              bool isInvalid);

    Index offsetInDataStreamBytes() const noexcept
    {
        return _offsetInDataStreamBytes;
    }

    Index offsetInDataStreamBits() const noexcept
    {
        return _offsetInDataStreamBytes * 8;
    }

    Index endOffsetInDataStreamBytes() const noexcept
    {
        return _offsetInDataStreamBytes + _effectiveTotalSize.bytes();
    }

    Index endOffsetInDataStreamBits() const noexcept
    {
        return this->endOffsetInDataStreamBytes() * 8;
    }

    const boost::optional<DataSize>& preambleSize() const noexcept
    {
        return _preambleSize;
    }

    const boost::optional<Index>& packetContextOffsetInPacketBits() const noexcept
    {
        return _packetContextOffsetInPacketBits;
    }

    const boost::optional<DataSize>& expectedTotalSize() const noexcept
    {
        return _expectedTotalSize;
    }

    const boost::optional<DataSize>& expectedContentSize() const noexcept
    {
        return _expectedContentSize;
    }

    const DataSize& effectiveTotalSize() const noexcept
    {
        return _effectiveTotalSize;
    }

    const DataSize& effectiveContentSize() const noexcept
    {
        return _effectiveContentSize;
    }

    const boost::optional<Timestamp>& tsBegin() const noexcept
    {
        return _tsBegin;
    }

    const boost::optional<Timestamp>& tsEnd() const noexcept
    {
        return _tsEnd;
    }

    const boost::optional<Index>& seqNum() const noexcept
    {
        return _seqNum;
    }

    const boost::optional<Index>& dataStreamId() const noexcept
    {
        return _dataStreamId;
    }

    const boost::optional<Size>& discardedEventRecordCounter() const noexcept
    {
        return _discardedEventRecordCounter;
    }

    Index indexInDataStream() const noexcept
    {
        return _indexInDataStream;
    }

    Index natIndexInDataStream() const noexcept
    {
        return _indexInDataStream + 1;
    }

    // can be `nullptr` if this entry is invalid
    const yactfr::DataStreamType *dataStreamType() const noexcept
    {
        return _dst;
    }

    bool isInvalid() const noexcept
    {
        return _isInvalid;
    }

    void isInvalid(const bool isInvalid) noexcept
    {
        _isInvalid = isInvalid;
    }

    const boost::optional<Size>& eventRecordCount() const noexcept
    {
        return _eventRecordCount;
    }

    void eventRecordCount(const boost::optional<Size>& eventRecordCount) noexcept
    {
        _eventRecordCount = eventRecordCount;
    }

    bool operator<(const PacketIndexEntry& other) const noexcept
    {
        return _indexInDataStream < other._indexInDataStream;
    }

    bool operator==(const PacketIndexEntry& other) const noexcept
    {
        return _indexInDataStream == other._indexInDataStream;
    }

private:
    const Index _indexInDataStream;
    const Size _offsetInDataStreamBytes;
    const boost::optional<Index> _packetContextOffsetInPacketBits;
    const boost::optional<DataSize> _preambleSize;
    const boost::optional<DataSize> _expectedTotalSize;
    const boost::optional<DataSize> _expectedContentSize;
    const DataSize _effectiveTotalSize;
    const DataSize _effectiveContentSize;
    const yactfr::DataStreamType *_dst;
    const boost::optional<Index> _dataStreamId;
    const boost::optional<Timestamp> _tsBegin;
    const boost::optional<Timestamp> _tsEnd;
    const boost::optional<Index> _seqNum;
    const boost::optional<Size> _discardedEventRecordCounter;
    bool _isInvalid;
    boost::optional<Size> _eventRecordCount;
};

} // namespace jacques

#endif // _JACQUES_PACKET_INDEX_ENTRY_HPP
