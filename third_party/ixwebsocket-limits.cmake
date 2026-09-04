# Bounded receive policy applied to a build-tree copy of the exact pinned source.
# The upstream gitlink remains unmodified. This is a quota patch, not a WS parser.
set(IX_TRANSPORT "${CMAKE_CURRENT_LIST_DIR}/ixwebsocket/ixwebsocket/IXWebSocketTransport.cpp")
file(SHA256 "${IX_TRANSPORT}" IX_HASH)
if(NOT IX_HASH STREQUAL "7d905fba49d7602ba5845f7852522fbdb1c82bd579350a3ea38e10e756247085")
  message(FATAL_ERROR "IXWebSocket quota patch source mismatch")
endif()
file(READ "${IX_TRANSPORT}" IX_TEXT)
string(REPLACE "const uint64_t maxFrameSize(1ULL << 63);" [=[
const uint64_t maxFrameSize = 1024 * 1024 + 36;
            uint64_t total = ws.N;
            for (const auto& chunk : _chunks) total += chunk.size();
            const bool textMessage = ws.opcode == wsheader_type::TEXT_FRAME ||
                (ws.opcode == wsheader_type::CONTINUATION && _fragmentedMessageKind == MessageKind::MSG_TEXT);
            if (total > (textMessage ? 8192 : maxFrameSize) || _chunks.size() >= 256) {
                close(1009, "FrameBridge receive quota");
                return;
            }
]=] IX_TEXT "${IX_TEXT}")
string(REPLACE "                _txbuf.erase(_txbuf.begin(), _txbuf.begin() + ret);" [=[
                _txbuf.erase(_txbuf.begin(), _txbuf.begin() + ret);
                // Yield after one write so a large fragmented message cannot
                // monopolize the poll loop ahead of inbound dispatch.
                return true;
]=] IX_TEXT "${IX_TEXT}")
string(REPLACE "                if (!sendOnSocket())\n                {\n                    return false;\n                }" [=[
                if (!sendOnSocket())
                {
                    return false;
                }
                return true;
]=] IX_TEXT "${IX_TEXT}")
string(REPLACE "        _blockingSend = true;" "        _blockingSend = false;" IX_TEXT "${IX_TEXT}")
file(WRITE "${CMAKE_BINARY_DIR}/IXWebSocketTransport.cpp" "${IX_TEXT}")
get_target_property(IX_SOURCES ixwebsocket SOURCES)
list(FILTER IX_SOURCES EXCLUDE REGEX "IXWebSocketTransport\\.cpp$")
set_property(TARGET ixwebsocket PROPERTY SOURCES "${IX_SOURCES};${CMAKE_BINARY_DIR}/IXWebSocketTransport.cpp")
target_include_directories(ixwebsocket PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ixwebsocket/ixwebsocket")
