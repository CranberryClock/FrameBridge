# Bounded receive policy applied to a build-tree copy of the exact pinned source.
# The upstream gitlink remains unmodified. This is a quota patch, not a WS parser.
set(IX_TRANSPORT "${CMAKE_CURRENT_LIST_DIR}/ixwebsocket/ixwebsocket/IXWebSocketTransport.cpp")
file(SHA256 "${IX_TRANSPORT}" IX_HASH)
if(NOT IX_HASH STREQUAL "4adddad5651737e80b80bd2d4fac5d1ac1e41bc037c9ad406909c44fdcdaa277")
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
file(WRITE "${CMAKE_BINARY_DIR}/IXWebSocketTransport.cpp" "${IX_TEXT}")
get_target_property(IX_SOURCES ixwebsocket SOURCES)
list(FILTER IX_SOURCES EXCLUDE REGEX "IXWebSocketTransport\\.cpp$")
set_property(TARGET ixwebsocket PROPERTY SOURCES "${IX_SOURCES};${CMAKE_BINARY_DIR}/IXWebSocketTransport.cpp")
target_include_directories(ixwebsocket PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ixwebsocket/ixwebsocket")
