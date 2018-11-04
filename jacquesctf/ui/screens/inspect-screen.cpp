/*
 * Copyright (C) 2018 Philippe Proulx <eepp.ca> - All Rights Reserved
 *
 * Unauthorized copying of this file, via any medium, is strictly
 * prohibited. Proprietary and confidential.
 */

#include <cassert>
#include <iostream>
#include <curses.h>
#include <signal.h>
#include <unistd.h>

#include "config.hpp"
#include "event-record-table-view.hpp"
#include "inspect-screen.hpp"
#include "stylist.hpp"
#include "state.hpp"
#include "packet-data-view.hpp"

namespace jacques {

InspectScreen::InspectScreen(const Rectangle& rect, const Config& cfg,
                             const Stylist& stylist, State& state) :
    Screen {rect, cfg, stylist, state},
    _decErrorView {
        std::make_unique<PacketDecodingErrorDetailsView>(rect, stylist, state)
    },
    _searchController {*this, stylist},
    _tsFormatModeWheel {
        TimestampFormatMode::LONG,
        TimestampFormatMode::NS_FROM_ORIGIN,
        TimestampFormatMode::CYCLES,
    },
    _dsFormatModeWheel {
        utils::SizeFormatMode::FULL_FLOOR_WITH_EXTRA_BITS,
        utils::SizeFormatMode::BYTES_FLOOR_WITH_EXTRA_BITS,
        utils::SizeFormatMode::BITS,
    },
    _currentStateSnapshot {std::end(_stateSnapshots)},
    _ertViewDisplayModeWheel {
        _ErtViewDisplayMode::SHORT,
        _ErtViewDisplayMode::LONG,
        _ErtViewDisplayMode::HIDDEN,
    }
{
    _pdView = std::make_unique<PacketDataView>(this->rect(), stylist, state,
                                               _bookmarks);
    _ertView = std::make_unique<EventRecordTableView>(this->rect(), stylist,
                                                      state);
    _priView = std::make_unique<PacketRegionInfoView>(Rectangle {{0, 0},
                                                                 this->rect().w, 1},
                                                      stylist, state);
    _sdteView = std::make_unique<SubDataTypeExplorerView>(this->rect(), stylist,
                                                          state);
    _decErrorView->isVisible(false);
    _pdView->focus();
    this->_updateViews();
}

InspectScreen::~InspectScreen()
{
}

void InspectScreen::_toggleBookmark(const unsigned int id)
{
    if (!this->_state().hasActivePacketState()) {
        return;
    }

    auto& bookmarks = _bookmarks[this->_state().activeDataStreamFileStateIndex()]
                                [this->_state().activeDataStreamFileState().activePacketStateIndex()];

    assert(id < bookmarks.size());

    auto& bookmarkedOffsetInPacketBit = bookmarks[id];

    if (bookmarkedOffsetInPacketBit) {
        if (bookmarkedOffsetInPacketBit == this->_state().curOffsetInPacketBits()) {
            bookmarkedOffsetInPacketBit = boost::none;
        } else {
            bookmarkedOffsetInPacketBit = this->_state().curOffsetInPacketBits();
        }
    } else {
        bookmarkedOffsetInPacketBit = this->_state().curOffsetInPacketBits();
    }

    _pdView->redraw();
}

void InspectScreen::_gotoBookmark(const unsigned int id)
{
    if (!this->_state().hasActivePacketState()) {
        return;
    }

    const auto& bookmark = _bookmarks[this->_state().activeDataStreamFileStateIndex()]
                                     [this->_state().activeDataStreamFileState().activePacketStateIndex()]
                                     [id];

    if (bookmark) {
        this->_state().gotoPacketRegionAtOffsetInPacketBits(*bookmark);
    }
}

void InspectScreen::_updateViews()
{
    Size ertViewHeight = 0;

    switch (_ertViewDisplayModeWheel.currentValue()) {
    case _ErtViewDisplayMode::SHORT:
        ertViewHeight = 8;
        break;

    case _ErtViewDisplayMode::LONG:
        ertViewHeight = this->rect().h / 2;
        break;

    default:
        break;
    }

    const auto pdViewHeight = this->rect().h - ertViewHeight - 1;
    Size pdViewWidth;

    if (_sdteViewIsVisible) {
        pdViewWidth = this->rect().w / 2;
    } else {
        pdViewWidth = this->rect().w;
    }

    _sdteView->isVisible();

    if (_ertViewDisplayModeWheel.currentValue() == _ErtViewDisplayMode::HIDDEN) {
        _ertView->moveAndResize({{0, 0}, this->rect().w, 8});
        _ertView->isVisible(false);
    } else {
        _ertView->moveAndResize({{0, pdViewHeight + 1}, this->rect().w,
                                 ertViewHeight});
        _ertView->isVisible(true);
    }

    _pdView->moveAndResize({{0, 0}, pdViewWidth, pdViewHeight});
    _priView->moveAndResize({{0, pdViewHeight}, this->rect().w, 1});

    if (_sdteViewIsVisible) {
        _sdteView->moveAndResize({{pdViewWidth, 0},
                                  this->rect().w - pdViewWidth,
                                  pdViewHeight});
    } else {
        _sdteView->moveAndResize({{0, 0}, this->rect().w, pdViewHeight});
    }

    _sdteView->isVisible(_sdteViewIsVisible);
    _decErrorView->moveAndResize({{this->rect().pos.x + 4,
                                   this->rect().h - 14},
                                  this->rect().w - 8, 12});
    _ertView->centerSelectedRow(false);
}

void InspectScreen::_redraw()
{
    _pdView->redraw();
    _ertView->redraw();
    _priView->redraw();
    _sdteView->redraw();
    _decErrorView->redraw();
}

void InspectScreen::_resized()
{
    this->_updateViews();
    _searchController.parentScreenResized(*this);
}

void InspectScreen::_visibilityChanged()
{
    _pdView->isVisible(this->isVisible());
    _ertView->isVisible(this->isVisible());
    _priView->isVisible(this->isVisible());

    if (this->isVisible()) {
        if (_stateSnapshots.empty()) {
            // initial snapshot
            this->_snapshotState();
        }

        _pdView->redraw();
        _ertView->redraw();
        _priView->redraw();
        this->_tryShowDecodingError();
        _decErrorView->refresh(true);
    }
}

void InspectScreen::_tryShowDecodingError()
{
    if (this->_state().hasActivePacketState() &&
            this->_state().activePacketState().packet().error()) {
        _decErrorView->moveAndResize(Rectangle {{this->rect().pos.x + 4,
                                                 this->rect().h - 14},
                                                this->rect().w - 8, 12});
        _decErrorView->isVisible(true);
    } else if (_decErrorView->isVisible()) {
        _decErrorView->isVisible(false);
    }
}

void InspectScreen::_snapshotState()
{
    _StateSnapshot snapshot;

    snapshot.dsfStateIndex = this->_state().activeDataStreamFileStateIndex();

    if (this->_state().hasActivePacketState()) {
        const auto& activeDsfState = this->_state().activeDataStreamFileState();

        snapshot.packetIndexInDataStreamFile = activeDsfState.activePacketStateIndex();
        snapshot.offsetInPacketBits = activeDsfState.curOffsetInPacketBits();
    }

    if (!_stateSnapshots.empty()) {
        if (*_currentStateSnapshot == snapshot) {
            // unchanged
            return;
        }

        const auto nextSnapshot = std::next(_currentStateSnapshot);

        if (nextSnapshot != std::end(_stateSnapshots)) {
            _stateSnapshots.erase(nextSnapshot, std::end(_stateSnapshots));
        }
    }

    _stateSnapshots.push_back(snapshot);
    _currentStateSnapshot = std::prev(std::end(_stateSnapshots));

    if (_stateSnapshots.size() > _maxStateSnapshots) {
        _stateSnapshots.pop_front();
        assert(_stateSnapshots.size() == _maxStateSnapshots);
    }
}

void InspectScreen::_goBack()
{
    if (_stateSnapshots.empty()) {
        return;
    }

    if (_currentStateSnapshot == std::begin(_stateSnapshots)) {
        // can't go back
        return;
    }

    --_currentStateSnapshot;
    this->_restoreStateSnapshot(*_currentStateSnapshot);
}

void InspectScreen::_goForward()
{
    if (_stateSnapshots.empty()) {
        return;
    }

    if (std::next(_currentStateSnapshot) == std::end(_stateSnapshots)) {
        // can't go forward
        return;
    }

    ++_currentStateSnapshot;
    this->_restoreStateSnapshot(*_currentStateSnapshot);
}

void InspectScreen::_restoreStateSnapshot(const _StateSnapshot& snapshot)
{
    this->_state().gotoDataStreamFile(snapshot.dsfStateIndex);

    if (snapshot.packetIndexInDataStreamFile) {
        this->_state().gotoPacket(*snapshot.packetIndexInDataStreamFile);
        assert(this->_state().hasActivePacketState());
        this->_state().gotoPacketRegionAtOffsetInPacketBits(snapshot.offsetInPacketBits);
    }
}

KeyHandlingReaction InspectScreen::_handleKey(const int key)
{
    const auto goingToBookmark = _goingToBookmark;

    _goingToBookmark = false;

    if (_decErrorView->isVisible()) {
        _decErrorView->isVisible(false);
        _pdView->redraw();
        _ertView->redraw();
        _priView->redraw();
    }

    switch (key) {
    case 127:
    case 8:
    case '9':
        this->_goBack();
        break;

    case '0':
        this->_goForward();
        break;

    case '#':
    case '`':
        this->_tryShowDecodingError();
        break;

    case 't':
        _tsFormatModeWheel.next();
        _ertView->timestampFormatMode(_tsFormatModeWheel.currentValue());
        break;

    case 's':
        _dsFormatModeWheel.next();
        _ertView->dataSizeFormatMode(_dsFormatModeWheel.currentValue());
        break;

    case 'e':
        _ertViewDisplayModeWheel.next();
        this->_updateViews();
        this->_redraw();
        break;

    case '\n':
    case '\r':
        _sdteViewIsVisible = !_sdteViewIsVisible;
        this->_updateViews();
        this->_redraw();
        break;

    case 'c':
        this->_state().gotoPacketContext();
        this->_snapshotState();
        break;

    case '1':
    case '2':
    case '3':
    case '4':
        if (goingToBookmark) {
            this->_gotoBookmark(key - '1');
            _pdView->redraw();
        } else {
            this->_toggleBookmark(key - '1');
            _pdView->redraw();
        }

        break;

    case 'b':
        _goingToBookmark = true;
        break;

    case KEY_HOME:
        this->_state().gotoPacketRegionAtOffsetInPacketBits(0);
        this->_snapshotState();
        break;

    case KEY_END:
        this->_state().gotoLastPacketRegion();
        this->_snapshotState();
        break;

    case KEY_LEFT:
        this->_state().gotoPreviousPacketRegion();
        this->_snapshotState();
        break;

    case KEY_RIGHT:
        this->_state().gotoNextPacketRegion();
        this->_snapshotState();
        break;

    case KEY_UP:
    {
        if (!this->_state().hasActivePacketState()) {
            break;
        }

        auto& packet = this->_state().activePacketState().packet();

        if (!this->_state().activePacketState().packet().hasData()) {
            break;
        }

        Index reqOffsetInPacketBits;

        if (this->_state().curOffsetInPacketBits() < _pdView->rowSize().bits()) {
            reqOffsetInPacketBits = 0;
        } else {
            reqOffsetInPacketBits = this->_state().curOffsetInPacketBits() -
                                    _pdView->rowSize().bits();
        }

        const auto& reqPacketRegion = packet.packetRegionAtOffsetInPacketBits(reqOffsetInPacketBits);

        this->_state().gotoPacketRegionAtOffsetInPacketBits(reqPacketRegion.segment().offsetInPacketBits());
        break;
    }

    case KEY_DOWN:
    {
        if (!this->_state().hasActivePacketState()) {
            break;
        }

        auto& packet = this->_state().activePacketState().packet();

        if (!packet.hasData()) {
            break;
        }

        const auto& curPacketRegion = *this->_state().currentPacketRegion();
        auto reqOffsetInPacketBits = std::max(this->_state().curOffsetInPacketBits() +
                                              _pdView->rowSize().bits(),
                                              curPacketRegion.segment().endOffsetInPacketBits());

        reqOffsetInPacketBits = std::min(reqOffsetInPacketBits,
                                         packet.lastPacketRegion().segment().offsetInPacketBits());

        const auto& reqPacketRegion = packet.packetRegionAtOffsetInPacketBits(reqOffsetInPacketBits);

        this->_state().gotoPacketRegionAtOffsetInPacketBits(reqPacketRegion.segment().offsetInPacketBits());
        break;
    }

    case KEY_PPAGE:
        _pdView->pageUp();
        break;

    case KEY_NPAGE:
        _pdView->pageDown();
        break;

    case '/':
    case 'g':
    {
        auto query = _searchController.start();

        if (!query) {
            // canceled or invalid
            _ertView->redraw();
            break;
        }

        this->_state().search(*query);

        /*
         * If we didn't move, the state snapshot will be identical and
         * will be not be pushed to the state snapshot list.
         */
        this->_snapshotState();

        _lastQuery = std::move(query);
        _ertView->redraw();
        break;
    }

    case 'n':
        if (!_lastQuery) {
            break;
        }

        this->_state().search(*_lastQuery);
        this->_snapshotState();
        _ertView->redraw();
        break;

    case '-':
        this->_state().gotoPreviousEventRecord();
        this->_snapshotState();
        break;

    case '+':
    case '=':
    case ' ':
        this->_state().gotoNextEventRecord();
        this->_snapshotState();
        break;

    case KEY_F(3):
        this->_state().gotoPreviousDataStreamFile();
        this->_snapshotState();
        this->_tryShowDecodingError();
        break;

    case KEY_F(4):
        this->_state().gotoNextDataStreamFile();
        this->_snapshotState();
        this->_tryShowDecodingError();
        break;

    case KEY_F(5):
        this->_state().gotoPreviousPacket();
        this->_snapshotState();
        this->_tryShowDecodingError();
        break;

    case KEY_F(6):
        this->_state().gotoNextPacket();
        this->_snapshotState();
        this->_tryShowDecodingError();
        break;

    case KEY_F(7):
        this->_state().gotoPreviousEventRecord(10);
        this->_snapshotState();
        break;

    case KEY_F(8):
        this->_state().gotoNextEventRecord(10);
        this->_snapshotState();
        break;

    default:
        break;
    }

    _pdView->refresh();
    _ertView->refresh();
    _priView->refresh();

    /*
     * Touch because the content could be unchanged from the last
     * refresh, and since this is overlapping other views, and they were
     * just refreshed, ncurses's optimization could ignore this refresh
     * otherwise.
     */
    _decErrorView->refresh(true);
    return KeyHandlingReaction::CONTINUE;
}

} // namespace jacques
