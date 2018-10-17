/*
 * Copyright (C) 2018 Philippe Proulx <eepp.ca> - All Rights Reserved
 *
 * Unauthorized copying of this file, via any medium, is strictly
 * prohibited. Proprietary and confidential.
 */

#ifndef _JACQUES_PACKET_HPP
#define _JACQUES_PACKET_HPP

#include <algorithm>
#include <memory>
#include <vector>
#include <yactfr/packet-sequence.hpp>
#include <yactfr/packet-sequence-iterator.hpp>
#include <yactfr/data-source.hpp>
#include <yactfr/metadata/float-type.hpp>
#include <yactfr/metadata/int-type.hpp>

#include "packet-index-entry.hpp"
#include "packet-checkpoints.hpp"
#include "event-record.hpp"
#include "data-region.hpp"
#include "content-data-region.hpp"
#include "packet-checkpoints-build-listener.hpp"
#include "metadata.hpp"
#include "memory-mapped-file.hpp"
#include "lru-cache.hpp"
#include "logging.hpp"

namespace jacques {

/*
 * This object's purpose is to provide data regions and event records to
 * views. This is the core data required by the packet inspection
 * activity.
 *
 * We keep a cache of data regions (content data regions being linked to
 * event records), and a cache of the event records linked in the data
 * region cache (for a faster access by index).
 *
 * Caching is valuable here because of how a typical packet inspection
 * session induces a locality of reference: you're typically going
 * backward or forward from the offset you're inspecting once you find a
 * location of interest, so there will typically be a lot of cache hits.
 *
 * The data region cache is a sorted vector of contiguous shared data
 * regions. The caching operation performed by
 * _ensureEventRecordIsCached() makes sure that all the data regions of
 * at most `_eventRecordCacheMaxSize` event records starting at the
 * requested index minus `_eventRecordCacheMaxSize / 2` are in cache.
 * Substracting `_eventRecordCacheMaxSize / 2` makes data regions and
 * event records available "around" the requested index, which makes
 * sense for a packet inspection activity because the user is typically
 * inspecting around a given offset.
 *
 * When a packet object is constructed, it caches everything known to be
 * in the preamble segment, that is, everything before the first event
 * record (if any), or all the packet's data regions otherwise
 * (including any padding or error data region before the end of the
 * packet). This preamble data region cache is kept as a separate cache
 * and copied back to the working cache when we make request an offset
 * located within it. When the working cache is the preamble cache, the
 * event record cache is empty.
 *
 *     preamble             ER 0          ER 1            ER 2
 *     #################----**********----***********-----********----
 *     ^^^^^^^^^^^^^^^^^^^^^
 *     preamble data region cache
 *
 *     preamble
 *     #########################------------
 *     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *     preamble data region cache
 *
 *     preamble                 error data region
 *     #########################!!!!!!!!!!!!!!!!!!!!!
 *     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *     preamble data region cache
 *
 *     preamble                           error data region
 *     #########################----------!!!!!!!!!!!
 *     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *     preamble data region cache
 *
 * If _cacheDataRegionsFromErsAtCurIt() adds anything to the data
 * region cache, if the last event record to be part of the cache is the
 * packet's last event record, it also adds any padding or error data
 * region before the end of the packet:
 *
 *        ER 176   ER 177       ER 294      ER 295         ER 296
 *     ...*******--******----...********----**********-----*******---...
 *                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 *        ER 300   ER 301       ER 487        ER 488
 *     ...*******--******----...**********----***********------------
 *                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 *        ER 300   ER 301       ER 487
 *     ...*******--******----...**********----********!!!!!!!!!!!!!!!
 *                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Because elements are naturally sorted in the caches, we can perform a
 * binary search to find a specific element using one of its
 * intrinsically ordered properties (index, offset in packet,
 * timestamp).
 *
 * As of this version, there's a single data region cache at a given
 * time. To help with the scenario where the user inspects the same
 * distant offsets often, we could create an LRU cache of data region
 * caches.
 *
 * There's also an LRU cache (offset in packet to data region) for
 * frequently accessed data regions by offset (with
 * dataRegionAtOffsetInPacketBits()): when there's a cache miss, the
 * method calls _ensureOffsetInPacketBitsIsCached() to update the data
 * region and event record caches and then adds the data region entry to
 * the LRU cache. The LRU cache avoids performing a binary search by
 * _dataRegionCacheItBeforeOrAtOffsetInPacketBits() every time.
 */
class Packet
{
public:
    using SP = std::shared_ptr<Packet>;

public:
    explicit Packet(const PacketIndexEntry& indexEntry,
                    yactfr::PacketSequence& seq,
                    const Metadata& metadata,
                    yactfr::DataSource::UP dataSrc,
                    std::unique_ptr<MemoryMappedFile> mmapFile,
                    PacketCheckpointsBuildListener& packetCheckpointsBuildListener);
    void appendDataRegions(std::vector<DataRegion::SP>& regions,
                           Index offsetInPacketBits,
                           Index endOffsetInPacketBits);
    const DataRegion& dataRegionAtOffsetInPacketBits(Index offsetInPacketBits);
    const DataRegion& firstDataRegion();
    const DataRegion& lastDataRegion();

    bool hasData() const noexcept
    {
        return _indexEntry->effectiveTotalSize() > 0;
    }

    const PacketIndexEntry& indexEntry() const noexcept
    {
        return *_indexEntry;
    }

    Size eventRecordCount() const noexcept
    {
        return _checkpoints.eventRecordCount();
    }

    const boost::optional<PacketDecodingError>& error() const noexcept
    {
        return _checkpoints.error();
    }

    const EventRecord& eventRecordAtIndexInPacket(const Index reqIndexInPacket)
    {
        assert(reqIndexInPacket < _checkpoints.eventRecordCount());
        this->_ensureEventRecordIsCached(reqIndexInPacket);
        return **_eventRecordCacheItFromIndexInPacket(reqIndexInPacket);
    }

    const EventRecord *firstEventRecord() const noexcept
    {
        if (_checkpoints.eventRecordCount() == 0) {
            return nullptr;
        }

        return _checkpoints.firstEventRecord().get();
    }

    const EventRecord *lastEventRecord() const noexcept
    {
        if (_checkpoints.eventRecordCount() == 0) {
            return nullptr;
        }

        return _checkpoints.lastEventRecord().get();
    }

private:
    using DataRegionCache = std::vector<DataRegion::SP>;
    using EventRecordCache = std::vector<EventRecord::SP>;

private:
    /*
     * Caches the whole packet preamble (single time): packet header,
     * packet context, and any padding until the first event record (if
     * any) or any padding/error until the end of the packet.
     *
     * Clears the event record cache.
     */
    void _cachePacketPreambleDataRegions();

    /*
     * Makes sure that the event record at index `indexInPacket` exists
     * in the caches. If it does not exist, this method caches the
     * requested event record as well as half of
     * `_eventRecordCacheMaxSize` event records around it (if possible),
     * centering the requested event record within its cache.
     */
    void _ensureEventRecordIsCached(Index indexInPacket);

    /*
     * Makes sure that a data region containing the bit
     * `offsetInPacketBits` exists in cache. If it does not exist, the
     * method finds the closest event record containing this bit and
     * calls _ensureEventRecordIsCached() with its index.
     */
    void _ensureOffsetInPacketBitsIsCached(Index offsetInPacketBits);

    /*
     * Appends all the remaining data regions starting at the current
     * iterator until any decoding error and then an error data region.
     */
    void _cacheDataRegionsAtCurItUntilError();

    /*
     * After clearing the caches, caches all the data regions from the
     * event records starting at the current iterator, for `erCount`
     * event records.
     */
    void _cacheDataRegionsFromErsAtCurIt(Index erIndexInPacket,
                                         Size erCount);

    /*
     * Appends a single event record (having index `indexInPacket`)
     * worth of data regions to the cache starting at the current
     * iterator.
     */
    void _cacheDataRegionsFromOneErAtCurIt(Index indexInPacket);

    /*
     * Appends data regions to the data region cache (and updates the
     * event record cache if needed) starting at the current iterator.
     * Stops appending _after_ the iterator's current element's kind is
     * `endElemKind`.
     */
    void _cacheDataRegionsAtCurIt(yactfr::Element::Kind endElemKind,
                                  bool setCurScope, bool setCurEventRecord,
                                  Index erIndexInPacket);

    /*
     * Tries to append a padding data region to the current cache, where
     * this data region would be located just before the current
     * iterator. This method uses the current cache's last data region
     * to know if there's padding, and if there is, what should be its
     * byte order. The padding data region is assigned scope `scope`
     * (can be `nullptr`).
     */
    void _tryCachePaddingDataRegionBeforeCurIt(Scope::SP scope);

    /*
     * Appends a content data region to the current cache from the
     * element(s) at the current iterator. Increments the current
     * iterator so that it contains the following element. The content
     * data region is assigned scope `scope` (cannot be `nullptr`).
     */
    void _cacheContentDataRegionAtCurIt(Scope::SP scope);

    /*
     * Returns whether or not the data region cache `cache` contains the
     * bit `offsetInPacketBits`.
     */
    bool _dataRegionCacheContainsOffsetInPacketBits(const DataRegionCache& cache,
                                                    const Index offsetInPacketBits) const
    {
        if (cache.empty()) {
            return false;
        }

        return offsetInPacketBits >= cache.front()->segment().offsetInPacketBits() &&
               offsetInPacketBits < cache.back()->segment().endOffsetInPacketBits();
    }

    /*
     * Returns the data region, within the current cache, of which the
     * offset is less than or equal to `offsetInPacketBits`.
     */
    DataRegionCache::iterator _dataRegionCacheItBeforeOrAtOffsetInPacketBits(const Index offsetInPacketBits)
    {
        assert(!_dataRegionCache.empty());
        assert(this->_dataRegionCacheContainsOffsetInPacketBits(_dataRegionCache,
                                                                offsetInPacketBits));

        const auto lessThanFunc = [](const auto& offsetInPacketBits,
                                     const auto dataRegion) {
            return offsetInPacketBits <
                   dataRegion->segment().offsetInPacketBits();
        };

        auto it = std::upper_bound(std::begin(_dataRegionCache),
                                   std::end(_dataRegionCache),
                                   offsetInPacketBits, lessThanFunc);
        assert(it != std::begin(_dataRegionCache));

        // we found one that is greater than, decrement once to find <=
        --it;
        assert((*it)->segment().offsetInPacketBits() <= offsetInPacketBits);
        return it;
    }

    /*
     * Returns the event record cache iterator containing the event
     * record having the index `indexInPacket`.
     */
    EventRecordCache::iterator _eventRecordCacheItFromIndexInPacket(const Index indexInPacket)
    {
        assert(!_eventRecordCache.empty());

        auto it = std::begin(_eventRecordCache);

        assert(indexInPacket >= (*it)->indexInPacket() &&
               indexInPacket < (*it)->indexInPacket() + _eventRecordCache.size());
        it += (indexInPacket - (*it)->indexInPacket());
        return it;
    }

    /*
     * Returns whether or not the event record having the index
     * `indexInPacket` exists in the caches.
     */
    bool _eventRecordIsCached(const Index indexInPacket) const
    {
        if (_eventRecordCache.empty()) {
            return false;
        }

        auto& firstEr = *_eventRecordCache.front();
        auto& lastEr = *_eventRecordCache.back();

        return indexInPacket >= firstEr.indexInPacket() &&
               indexInPacket <= lastEr.indexInPacket();
    }

    /*
     * Current iterator's offset (bits) within the packet.
     */
    Index _itOffsetInPacketBits() const
    {
        return _it.offset() - _indexEntry->offsetInDataStreamBits();
    }

    /*
     * Current iterator's offset (bytes, floored) within the packet.
     */
    Index _itOffsetInPacketBytes() const
    {
        return this->_itOffsetInPacketBits() / 8;
    }

    /*
     * For a given data segment, returns the corresponding data range.
     */
    DataRegion::DataRange _dataRangeForSegment(const DataSegment& segment) const
    {
        const auto bufAt = _mmapFile->addr() +
                           segment.offsetInPacketBits() / 8;
        const auto bytesToCopy = (segment.offsetInPacketExtraBits() +
                                  segment.size().bits() + 7) / 8;

        return DataRegion::DataRange {bufAt, bufAt + bytesToCopy};
    }

    DataRegion::DataRange _dataRangeAtCurIt(const Size sizeBits) const
    {
        return this->_dataRangeForSegment(DataSegment {this->_itOffsetInPacketBytes(),
                                                       sizeBits});
    }

    /*
     * Creates a content data region from the current iterator's element
     * known to have type `ElemT`. The content data region is assigned
     * scope `scope` (cannot be `nullptr`).
     */
    template <typename ElemT>
    ContentDataRegion::SP _contentDataRegionFromBitArrayElemAtCurIt(Scope::SP scope)
    {
        auto& elem = static_cast<const ElemT&>(*_it);
        const DataSegment segment {this->_itOffsetInPacketBits(),
                                   elem.type().size()};
        const auto dataRange = this->_dataRangeAtCurIt(elem.type().size());

        // okay to move the scope here, it's never used afterwards
        return std::make_shared<ContentDataRegion>(segment,
                                                   dataRange,
                                                   std::move(scope),
                                                   elem.type(),
                                                   ContentDataRegion::Value {elem.value()});
    }

    void _trySetPreviousDataRegionOffsetInPacketBits(DataRegion& dataRegion) const
    {
        if (_dataRegionCache.empty()) {
            return;
        }

        dataRegion.previousDataRegionOffsetInPacketBits(_dataRegionCache.back()->segment().offsetInPacketBits());
    }

private:
    const PacketIndexEntry * const _indexEntry;
    const Metadata * const _metadata;
    yactfr::DataSource::UP _dataSrc;
    std::unique_ptr<MemoryMappedFile> _mmapFile;
    yactfr::PacketSequenceIterator _it;
    yactfr::PacketSequenceIterator _endIt;
    PacketCheckpoints _checkpoints;
    DataRegionCache _dataRegionCache;
    DataRegionCache _preambleDataRegionCache;
    EventRecordCache _eventRecordCache;
    LruCache<Index, DataRegion::SP> _lruDataRegionCache;
    const Size _eventRecordCacheMaxSize = 500;
    const DataSize _preambleSize;
};

} // namespace jacques

#endif // _JACQUES_PACKET_HPP