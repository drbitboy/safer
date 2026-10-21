// inbound_unpacker.hpp
//
// The receiving counterpart of outbound_archiver.hpp: unpacks the
// archive delivered by the link sender into the inbound mailbox
// directory.
//
//   <linkDir>/sending.zip     delivered by the link sender (inbound link
//                             dir, default <inboundDir>/link); entries are
//                             named init.<type>.<dev>.<prop>.<elem>
//   <inboundDir>/init.<...>   entries extracted here first
//   <inboundDir>/ready.<...>  ...then renamed to this, replacing any older
//                             ready.<...> for the same key (latest wins)
//
// Steps performed by unpackInbound(), in order:
//   0. If <linkDir>/sending.zip does not exist, do nothing.
//   1. Delete stale init.<5 fields> files in inboundDir (left by a crash
//      mid-unpack; this function is the only writer of them).
//   2. Validate the archive BEFORE extracting anything: it must open as a
//      zip, contain at least one entry, and every entry must be a regular
//      file named exactly init.<type>.<dev>.<prop>.<elem> (five dot-
//      separated fields, no '/'), with no duplicate names. This also
//      prevents an entry from writing outside inboundDir.
//   3. Extract every entry to inboundDir/init.<...> (miniz verifies each
//      entry's CRC), then fsync them.
//   4. Only if ALL entries extracted cleanly (all-or-nothing): rename each
//      init.<...> to ready.<...>, then fsync the directory.
//   5. Delete sending.zip LAST. That is what lets the link sender deliver
//      the next archive, so a crash before this point just means the same
//      archive is unpacked again after restart, which rewrites the same
//      files with the same values.
//
// A bad archive (fails validation, or any entry fails to extract) is moved
// to <linkDir>/rejected.zip.YYYYmmddHHMMSS (UTC; ".1", ".2", ... appended
// if that name exists) and nothing is published: no ready.<...> files are
// created and any init.<...> files already extracted are removed. This
// unblocks the pipeline at the cost of dropping that archive's data, so
// callers should log it loudly.
//
// I/O errors that are not the archive's fault (cannot create a directory,
// a rename fails) return Error and leave sending.zip in place for a retry.
//
// Single-writer assumption: nothing else creates init.<...> or ready.<...>
// files in inboundDir while this runs (saf_local's own loop is single
// threaded and the other inbound-mailbox steps only claim/erase).

#pragma once
#include <string>

namespace saf {

enum class UnpackResult {
    NothingToDo, // no sending.zip
    Unpacked,    // all entries published as ready.<...>; sending.zip deleted
    Rejected,    // bad archive moved aside; see UnpackReport::rejectedPath
    Error,       // see UnpackReport::error; sending.zip left for a retry
};

struct UnpackReport {
    UnpackResult result = UnpackResult::NothingToDo;
    int fileCount = 0;         // records published (Unpacked)
    std::string error;         // Error: what failed; Rejected: why
    std::string rejectedPath;  // Rejected: where the archive now lives
};

UnpackReport unpackInbound(const std::string& inboundDir,
                           const std::string& linkDir);

} // namespace saf
