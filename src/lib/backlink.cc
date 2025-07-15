#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

namespace SnapperNS
{
  Genode::Attempt<Snapper::VERSION, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_version (void)
  {
    Snapper::VERSION version = 0;

    try
      {
        Genode::Readonly_file reader (snapper->snapper_root, value);
        Genode::Readonly_file::At pos{ 0 };

        char _version_buf[sizeof (Snapper::VERSION)];
        Genode::Byte_range_ptr version_buf (_version_buf,
                                            sizeof (Snapper::VERSION));

        if (reader.read (pos, version_buf) == 0)
          {
            Genode::error ("backlink missing version: ", value);

            return Genode::Attempt<Snapper::VERSION,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        version = *(reinterpret_cast<Snapper::VERSION *> (version_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        return Genode::Attempt<Snapper::VERSION,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::VERSION,
                           Snapper::Archive::Backlink::Error> (version);
  }

  Genode::Attempt<Snapper::CRC, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_integrity (void)
  {
    Snapper::CRC crc = 0;

    try
      {
        Genode::Readonly_file reader (snapper->snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION) };

        char _crc_buf[sizeof (Snapper::CRC)];
        Genode::Byte_range_ptr crc_buf (_crc_buf, sizeof (Snapper::CRC));

        if (reader.read (pos, crc_buf) == 0)
          {
            Genode::error ("backlink missing CRC: ", value);

            return Genode::Attempt<Snapper::CRC,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        crc = *(reinterpret_cast<Snapper::CRC *> (crc_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        return Genode::Attempt<Snapper::CRC,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::CRC, Snapper::Archive::Backlink::Error> (
        crc);
  }

  Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_reference_count (void)
  {
    Snapper::RC reference_count = 0;

    try
      {
        Genode::Readonly_file reader (snapper->snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::CRC) };

        char _rc_buf[sizeof (Snapper::RC)];
        Genode::Byte_range_ptr rc_buf (_rc_buf, sizeof (Snapper::RC));

        if (reader.read (pos, rc_buf) == 0)
          {
            Genode::error ("backlink missing reference count: ", value);
            return Genode::Attempt<Snapper::RC,
                                   Snapper::Archive::Backlink::Error> (
                MissingFieldErr);
          }

        reference_count = *(reinterpret_cast<Snapper::RC *> (rc_buf.start));
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open backlink: ", value);
        return Genode::Attempt<Snapper::RC,
                               Snapper::Archive::Backlink::Error> (OpenErr);
      }

    return Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
        reference_count);
  }

  Genode::Attempt<Genode::size_t, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::get_data_size (void)
  {
    Vfs::file_size fsize = 0;

    try
      {
        fsize = snapper->snapper_root.file_size (value);
      }
    catch (Genode::Directory::Nonexistent_file)
      {
        Genode::error ("could not data size of nonexistent file: ", value);
        return Genode::Attempt<Genode::size_t,
                               Snapper::Archive::Backlink::Error> (
            Snapper::Archive::Backlink::StatsErr);
      }

    Genode::size_t size = fsize - sizeof (Snapper::VERSION)
                          - sizeof (Snapper::CRC) - sizeof (Snapper::RC);

    return Genode::Attempt<Genode::size_t, Snapper::Archive::Backlink::Error> (
        size);
  }

  Snapper::Archive::Backlink::Error
  Snapper::Archive::Backlink::get_data (Genode::Byte_range_ptr &data)
  {
    Snapper::Archive::Backlink::Error err = None;

    get_data_size ().with_result (
        [&err, &data] (Genode::size_t size) {
          if (size > data.num_bytes)
            {
              Genode::error (
                  "insufficient buffer size to read from snapshot file!");

              err = InsufficientSizeErr;
              return;
            }
        },
        [&err] (Snapper::Archive::Backlink::Error e) { err = e; });

    if (err != None)
      {
        return err;
      }

    try
      {
        Genode::Readonly_file reader (snapper->snapper_root, value);
        Genode::Readonly_file::At pos{ sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::CRC)
                                       + sizeof (Snapper::RC) };

        if (reader.read (pos, data) == 0)
          {
            Genode::error ("backlink missing data: ", value);
            return MissingFieldErr;
          }
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open backlink: ", value);
        return OpenErr;
      }

    return None;
  }

  Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error>
  Snapper::Archive::Backlink::set_reference_count (
      const Snapper::RC reference_count)
  {
    Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> res (
        reference_count);

    Snapper::VERSION version = 0;
    Snapper::CRC crc = 0;

    get_version ().with_result (
        [&version] (Snapper::VERSION _version) { version = _version; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    get_integrity ().with_result (
        [&crc] (Snapper::CRC _crc) { crc = _crc; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    Genode::size_t data_size;

    get_data_size ().with_result (
        [&data_size] (Genode::size_t size) { data_size = size; },
        [&res] (Snapper::Archive::Backlink::Error err) {
          res = Genode::Attempt<Snapper::RC,
                                Snapper::Archive::Backlink::Error> (err);
        });

    if (res.failed ())
      {
        Genode::error ("couldn't update the reference count of: ", value);
        return res;
      }

    char *_data_buf = (char *)snapper->heap.alloc (data_size);
    Genode::Byte_range_ptr data (_data_buf, data_size);

    Snapper::Archive::Backlink::Error err = get_data (data);
    if (err != None)
      {
        res = Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
            err);

        goto CLEAN_RET;
      }

    snapper->snapper_root.unlink (value);

    try
      {
        Genode::New_file writer (snapper->snapper_root, value);

        // version
        Genode::New_file::Append_result write_res
            = writer.append ((char *)&version, sizeof (Snapper::VERSION));

        if (write_res != Genode::New_file::Append_result::OK)
          {
            res = Genode::Attempt<Snapper::RC,
                                  Snapper::Archive::Backlink::Error> (
                WriteErr);

            goto CLEAN_RET;
          }

        // integrity
        write_res = writer.append ((char *)&crc, sizeof (Snapper::CRC));

        if (write_res != Genode::New_file::Append_result::OK)
          {
            res = Genode::Attempt<Snapper::RC,
                                  Snapper::Archive::Backlink::Error> (
                WriteErr);

            goto CLEAN_RET;
          }

        // reference count
        write_res
            = writer.append ((char *)&reference_count, sizeof (Snapper::RC));

        if (write_res != Genode::New_file::Append_result::OK)
          {
            res = Genode::Attempt<Snapper::RC,
                                  Snapper::Archive::Backlink::Error> (
                WriteErr);

            goto CLEAN_RET;
          }

        // data
        write_res = writer.append (data.start, data.num_bytes);

        if (write_res != Genode::New_file::Append_result::OK)
          {
            res = Genode::Attempt<Snapper::RC,
                                  Snapper::Archive::Backlink::Error> (
                WriteErr);

            goto CLEAN_RET;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        res = Genode::Attempt<Snapper::RC, Snapper::Archive::Backlink::Error> (
            OpenErr);

        goto CLEAN_RET;
      }

  CLEAN_RET:
    if (data.start)
      snapper->heap.free (data.start, data.num_bytes);

    if (res == WriteErr)
      {
        Genode::error ("could not update reference count: ", value);
      }

    return res;
  }
};
