#include "metadata_parser.h"
#include "common.h"
#include "database.h"
#include "logging.h"
#include "string_utils.h"
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to convert sync-safe integer */
static int decode_syncsafe(unsigned char *b) {
  return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

static int decode_int(unsigned char *b) {
  return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

/* Read 32-bit little-endian integer */
static int read_le32(unsigned char *b) {
  return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

/* Read 32-bit big-endian integer (for MP4) */
static int read_be32(unsigned char *b) {
  return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

/* Parse M4A/MP4 metadata atoms */
static bool parse_m4a_metadata(FILE *f, Track *out_track, char *out_artist,
                               char *out_album) {
  unsigned char buf[8];
  bool found_title = false;
  bool found_artist = false;

  /* bool found_album = false; removed unused */

  /* Check for ftyp atom (MP4 signature) */
  fseek(f, 0, SEEK_SET);
  if (fread(buf, 1, 8, f) != 8)
    return false;

  int ftyp_size = read_be32(buf);
  if (memcmp(buf + 4, "ftyp", 4) != 0)
    return false;

  log_message("INFO", "parse_m4a: Valid MP4/M4A file");

  /* Skip ftyp atom */
  fseek(f, ftyp_size, SEEK_SET);

  /* Scan for moov atom */
  while (!feof(f)) {
    if (fread(buf, 1, 8, f) != 8)
      break;

    int atom_size = read_be32(buf);
    if (atom_size < 8 || atom_size > 100 * 1024 * 1024)
      break;

    /* Check for moov atom */
    if (memcmp(buf + 4, "moov", 4) == 0) {
      log_message("INFO", "parse_m4a: Found moov atom");
      long moov_end = ftell(f) + atom_size - 8;

      /* Scan inside moov for udta atom */
      while (ftell(f) < moov_end) {
        if (fread(buf, 1, 8, f) != 8)
          break;
        int udta_size = read_be32(buf);
        if (udta_size < 8)
          break;

        if (memcmp(buf + 4, "udta", 4) == 0) {
          log_message("INFO", "parse_m4a: Found udta atom");
          long udta_end = ftell(f) + udta_size - 8;

          /* Scan for meta atom */
          while (ftell(f) < udta_end) {
            if (fread(buf, 1, 8, f) != 8)
              break;
            int meta_size = read_be32(buf);
            if (meta_size < 8)
              break;

            if (memcmp(buf + 4, "meta", 4) == 0) {
              fseek(f, 4, SEEK_CUR); /* Skip version/flags */
              long meta_end = ftell(f) + meta_size - 12;

              /* Scan for ilst atom (item list) */
              while (ftell(f) < meta_end) {
                if (fread(buf, 1, 8, f) != 8)
                  break;
                int ilst_size = read_be32(buf);
                if (ilst_size < 8)
                  break;

                if (memcmp(buf + 4, "ilst", 4) == 0) {
                  log_message("INFO", "parse_m4a: Found ilst atom");
                  long ilst_end = ftell(f) + ilst_size - 8;

                  /* Scan items in ilst */
                  while (ftell(f) < ilst_end) {
                    if (fread(buf, 1, 8, f) != 8)
                      break;
                    int item_size = read_be32(buf);
                    if (item_size < 8)
                      break;

                    long item_end = ftell(f) + item_size - 8;

                    /* ©nam = Title (0xA9 = copyright symbol) */
                    if (buf[4] == 0xA9 && memcmp(buf + 5, "nam", 3) == 0) {
                      /* Read data atom inside */
                      if (fread(buf, 1, 8, f) == 8 &&
                          memcmp(buf + 4, "data", 4) == 0) {
                        fseek(f, 8, SEEK_CUR); /* Skip type/locale */
                        int data_len = read_be32(buf) - 16;
                        if (data_len > 0 && data_len < MAX_SONG_TITLE) {
                          if (fread(out_track->title, 1, data_len, f) ==
                              (size_t)data_len) {
                            out_track->title[data_len] = '\0';
                            found_title = true;
                            log_message("INFO", "parse_m4a: Found title");
                          }
                        }
                      }
                    }
                    /* ©ART = Artist */
                    else if (buf[4] == 0xA9 && memcmp(buf + 5, "ART", 3) == 0) {
                      if (fread(buf, 1, 8, f) == 8 &&
                          memcmp(buf + 4, "data", 4) == 0) {
                        fseek(f, 8, SEEK_CUR);
                        int data_len = read_be32(buf) - 16;
                        if (data_len > 0 && data_len < 256) {
                          if (fread(out_artist, 1, data_len, f) ==
                              (size_t)data_len) {
                            out_artist[data_len] = '\0';
                            found_artist = true;
                            log_message("INFO", "parse_m4a: Found artist");
                          }
                        }
                      }
                    }
                    /* ©alb = Album */
                    else if (buf[4] == 0xA9 && memcmp(buf + 5, "alb", 3) == 0) {
                      if (fread(buf, 1, 8, f) == 8 &&
                          memcmp(buf + 4, "data", 4) == 0) {
                        fseek(f, 8, SEEK_CUR);
                        int data_len = read_be32(buf) - 16;
                        if (data_len > 0 && data_len < 256) {
                          if (fread(out_album, 1, data_len, f) ==
                              (size_t)data_len) {
                            out_album[data_len] = '\0';
                            out_album[data_len] = '\0';
                            /* found_album = true; removed unused */
                            log_message("INFO", "parse_m4a: Found album");
                          }
                        }
                      }
                    }
                    /* trkn = Track Number */
                    else if (memcmp(buf + 4, "trkn", 4) == 0) {
                      if (fread(buf, 1, 8, f) == 8 &&
                          memcmp(buf + 4, "data", 4) == 0) {
                        fseek(f, 10, SEEK_CUR); /* Skip type/locale/padding */
                        unsigned char trk_buf[2];
                        if (fread(trk_buf, 1, 2, f) == 2) {
                          out_track->track_number =
                              (trk_buf[0] << 8) | trk_buf[1];
                          log_message("INFO", "parse_m4a: Found track number");
                        }
                      }
                    }
                    /* ©day = Year */
                    else if (buf[4] == 0xA9 && memcmp(buf + 5, "day", 3) == 0) {
                      if (fread(buf, 1, 8, f) == 8 &&
                          memcmp(buf + 4, "data", 4) == 0) {
                        fseek(f, 8, SEEK_CUR);
                        int data_len = read_be32(buf) - 16;
                        char year_str[16] = {0};
                        if (data_len > 0 && data_len < 15) {
                          if (fread(year_str, 1, data_len, f) ==
                              (size_t)data_len) {
                            out_track->year = atoi(year_str);
                            log_message("INFO", "parse_m4a: Found year");
                          }
                        }
                      }
                    }

                    fseek(f, item_end, SEEK_SET);
                  }

                  return (found_title || found_artist);
                }
                fseek(f, ftell(f) + ilst_size - 8, SEEK_SET);
              }
            }
            fseek(f, ftell(f) + meta_size - 8, SEEK_SET);
          }
        }
        fseek(f, ftell(f) + udta_size - 8, SEEK_SET);
      }
      break;
    }

    /* Skip this atom */
    fseek(f, ftell(f) + atom_size - 8, SEEK_SET);
  }

  return (found_title || found_artist);
}

/* Read 32-bit little-endian integer as unsigned */
static uint32_t read_le32u(unsigned char *b) {
  return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

/* Parse WAV RIFF INFO chunks for metadata */
static bool parse_wav_metadata(FILE *f, char *out_title, char *out_artist,
                               int max_len) {
  unsigned char buf[12];
  bool found_title = false;
  bool found_artist = false;

  /* Look for RIFF header */
  fseek(f, 0, SEEK_SET);
  if (fread(buf, 1, 12, f) != 12)
    return false;

  /* Check for RIFF/WAVE */
  if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
    return false;
  }

  uint32_t file_size = read_le32u(buf + 4);
  /* If file_size is small, stick to it. If it seems huge/invalid, we'll rely on
   * feof/chunk headers */

  uint32_t pos = 12; // Start after RIFF header

  /* Scan chunks looking for LIST/INFO or ID3 */
  /* Increased limit to scanning 500MB to catch tags at end of large files */
  while (pos < 500 * 1024 * 1024) {
    unsigned char chunk_hdr[8];
    fseek(f, pos, SEEK_SET);
    size_t read_bytes = fread(chunk_hdr, 1, 8, f);
    if (read_bytes != 8)
      break; /* End of file or read error */

    uint32_t chunk_size = read_le32u(chunk_hdr + 4);

    /* Log chunk found (debug) */
    /*
    char chunk_name[5] = {0};
    memcpy(chunk_name, chunk_hdr, 4);
    char msg[128];
    snprintf(msg, sizeof(msg), "parse_wav: Chunk='%s' size=%u", chunk_name,
    chunk_size); log_message("INFO", msg);
    */

    /* Check for LIST chunk */
    if (memcmp(chunk_hdr, "LIST", 4) == 0) {
      unsigned char list_type[4];
      if (fread(list_type, 1, 4, f) != 4)
        break;

      /* Check for INFO list */
      if (memcmp(list_type, "INFO", 4) == 0) {
        log_message("INFO", "parse_wav: Found LIST INFO chunk");
        uint32_t list_end = pos + 8 + chunk_size;
        uint32_t info_pos = pos + 12; // 8 bytes header + 4 bytes 'INFO'

        /* Parse INFO sub-chunks */
        while (info_pos < list_end - 8) {
          unsigned char info_hdr[8];
          fseek(f, info_pos, SEEK_SET);
          if (fread(info_hdr, 1, 8, f) != 8)
            break;

          uint32_t info_size = read_le32u(info_hdr + 4);

          /* Sanity check size */
          if (info_size > 1024 * 10) { /* If subchunk seems impossibly large,
                                          likely bad parse */
            /* Try to recover or skip */
            break;
          }

          /* INAM = Title */
          if (memcmp(info_hdr, "INAM", 4) == 0 && info_size > 0) {
            int read_len =
                (info_size < (uint32_t)max_len) ? info_size : max_len - 1;
            if (fread(out_title, 1, read_len, f) == (size_t)read_len) {
              out_title[read_len] = '\0';
              /* Remove trailing nulls/whitespace */
              while (read_len > 0 && (out_title[read_len - 1] == '\0' ||
                                      out_title[read_len - 1] == ' ')) {
                out_title[--read_len] = '\0';
              }
              found_title = true;
              log_message("INFO", "parse_wav: Found title (INAM)");
            }
          }
          /* IART = Artist */
          else if (memcmp(info_hdr, "IART", 4) == 0 && info_size > 0) {
            int read_len =
                (info_size < (uint32_t)max_len) ? info_size : max_len - 1;
            if (fread(out_artist, 1, read_len, f) == (size_t)read_len) {
              out_artist[read_len] = '\0';
              while (read_len > 0 && (out_artist[read_len - 1] == '\0' ||
                                      out_artist[read_len - 1] == ' ')) {
                out_artist[--read_len] = '\0';
              }
              found_artist = true;
              log_message("INFO", "parse_wav: Found artist (IART)");
            }
          }

          /* Move to next sub-chunk (word-aligned) */
          info_pos += 8 + info_size;
          if (info_size % 2 != 0)
            info_pos++;
        }

        if (found_title || found_artist) {
          return true;
        }
      }
    }
    /* Check for ID3 chunk (ID3v2 tags embedded in WAV) */
    else if (memcmp(chunk_hdr, "ID3 ", 4) == 0 ||
             memcmp(chunk_hdr, "id3 ", 4) == 0) {
      log_message("INFO", "parse_wav: Found ID3 chunk in WAV file");

      /* Read the ID3 data from this chunk and parse as ID3v2 */
      /* The chunk contains ID3v2 header starting with "ID3" */
      unsigned char id3_hdr[10];
      if (fread(id3_hdr, 1, 10, f) == 10 && memcmp(id3_hdr, "ID3", 3) == 0) {

        int id3_size = decode_syncsafe(&id3_hdr[6]);
        if (id3_size > 0 && (uint32_t)id3_size <= chunk_size) {
          /* Basic ID3 scanning logic similar to previous, but careful with
           * types */
          /* Simple Scan for TIT2/TPE1 */
          int scan_pos = 0;
          while (scan_pos < id3_size - 10 && scan_pos < 50000) {
            unsigned char frame_hdr[10];
            if (fread(frame_hdr, 1, 10, f) != 10)
              break;

            if (frame_hdr[0] == 0)
              break; // Padding

            int frame_size = decode_int(&frame_hdr[4]);
            if (frame_size <= 0 ||
                frame_size > 1024) { /* Sanity check frame size */
              scan_pos +=
                  1; /* scan byte by byte if lost? No, just abort this chunk */
              break;
            }

            if (memcmp(frame_hdr, "TPE1", 4) == 0) {
              fseek(f, 1, SEEK_CUR); // encoding
              int read_len =
                  (frame_size - 1 < max_len) ? frame_size - 1 : max_len - 1;
              if (read_len > 0)
                fread(out_artist, 1, read_len, f);
              out_artist[read_len] = 0;
              found_artist = true;
            } else if (memcmp(frame_hdr, "TIT2", 4) == 0) {
              fseek(f, 1, SEEK_CUR);
              int read_len =
                  (frame_size - 1 < max_len) ? frame_size - 1 : max_len - 1;
              if (read_len > 0)
                fread(out_title, 1, read_len, f);
              out_title[read_len] = 0;
              found_title = true;
            } else {
              fseek(f, frame_size, SEEK_CUR);
            }
            scan_pos += 10 + frame_size;

            /* Seek to next frame start relative to current file pos */
            /* We just did freads/fseeks, so we are at next frame */
          }
        }
      }
      if (found_title || found_artist)
        return true;
    }

    /* Move to next chunk (word-aligned) */
    pos += 8 + chunk_size;
    if (chunk_size % 2 != 0)
      pos++;
  }

  return (found_title || found_artist);
}

/* WAV Duration: size / byte_rate */
static float get_wav_duration(FILE *f, uint32_t file_size) {
  unsigned char buf[12];
  fseek(f, 12, SEEK_SET); /* Skip RIFF header */

  uint32_t pos = 12;
  /* Scan for 'fmt ' chunk */
  while (pos < file_size && pos < 1024 * 1024) { /* Limit scan */
    unsigned char chunk_hdr[8];
    if (fread(chunk_hdr, 1, 8, f) != 8)
      break;

    uint32_t chunk_size = read_le32u(chunk_hdr + 4);

    if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
      if (chunk_size >= 16) {
        unsigned char fmt_data[16];
        if (fread(fmt_data, 1, 16, f) == 16) {
          uint32_t byte_rate = read_le32u(fmt_data + 8);
          if (byte_rate > 0) {
            /* Duration = (File Size - Header) / ByteRate */
            /* Checking file size again or trust passed size? */
            /* Approximate: use Total File Size / ByteRate. Close enough. */
            return (float)file_size / (float)byte_rate;
          }
        }
      }
      return 0.0f;
    }

    pos += 8 + chunk_size;
    fseek(f, pos, SEEK_SET);
  }
  return 0.0f;
}

/* M4A Duration: Duration / Timescale from 'mvhd' atom */
static float get_m4a_duration(FILE *f) {
  unsigned char buf[8];
  fseek(f, 0, SEEK_SET);
  if (fread(buf, 1, 8, f) != 8)
    return 0.0f;

  uint32_t ftyp_size = read_be32(buf);
  fseek(f, ftyp_size, SEEK_SET);

  while (!feof(f)) {
    if (fread(buf, 1, 8, f) != 8)
      break;
    uint32_t atom_size = read_be32(buf);
    if (atom_size < 8)
      break;

    if (memcmp(buf + 4, "moov", 4) == 0) {
      long moov_start = ftell(f);
      long moov_end = moov_start + atom_size - 8;

      while (ftell(f) < moov_end) {
        if (fread(buf, 1, 8, f) != 8)
          break;
        uint32_t sub_size = read_be32(buf);
        if (sub_size < 8)
          break;

        if (memcmp(buf + 4, "mvhd", 4) == 0) {
          /* Found Movie Header */
          unsigned char mvhd_data[100];
          if (fread(mvhd_data, 1, sub_size - 8, f) == sub_size - 8) {
            /* Version is byte 0 */
            int version = mvhd_data[0];
            uint32_t timescale, duration;
            if (version == 0) {
              timescale = read_be32(mvhd_data + 12);
              duration = read_be32(mvhd_data + 16);
            } else {
              timescale = read_be32(mvhd_data + 20);
              /* Duration is 64-bit in version 1, take lower 32 for now or parse
               * full */
              /* We'll stick to 32-bit logic for simplicity or check spec */
              /* offset 24 is high, 28 is low */
              duration = read_be32(mvhd_data + 28);
            }
            if (timescale > 0) {
              return (float)duration / (float)timescale;
            }
          }
          return 0.0f;
        }

        fseek(f, ftell(f) + sub_size - 8, SEEK_SET);
      }
      break;
    }

    fseek(f, ftell(f) + atom_size - 8, SEEK_SET);
  }
  return 0.0f;
}

/* =========================================================
 *              FLAC Metadata Parsing
 * ========================================================= */

/* Read bits from a buffer */
static uint64_t get_bits(const unsigned char *buffer, int *bit_offset,
                         int num_bits) {
  uint64_t result = 0;
  for (int i = 0; i < num_bits; i++) {
    int byte_idx = *bit_offset / 8;
    int bit_idx = 7 - (*bit_offset % 8);
    result = (result << 1) | ((buffer[byte_idx] >> bit_idx) & 1);
    (*bit_offset)++;
  }
  return result;
}

static bool parse_flac_metadata(FILE *f, Track *out_track, char *out_artist,
                                char *out_album) {
  /* FLAC marker already checked or we are at start */
  /* If called from get_metadata, we are after "fLaC" */
  /* But let's just make sure we are where we think we are?
     get_metadata read the first 10 bytes (header) which included "fLaC".
     If we assume we are passed the file stream at position 4 (after "fLaC"):
  */
  fseek(f, 4, SEEK_SET);

  bool unknown_block = false;
  bool is_last = false;
  unsigned char header[4];
  bool found_info = false;

  while (!is_last) {
    if (fread(header, 1, 4, f) != 4)
      break;

    is_last = (header[0] & 0x80) != 0;
    int type = header[0] & 0x7F;
    int length = (header[1] << 16) | (header[2] << 8) | header[3];

    if (type == 0) { /* STREAMINFO */
      unsigned char *data = (unsigned char *)malloc(length);
      if (data) {
        if (fread(data, 1, length, f) == (size_t)length) {
          /* Min Block (16), Max Block (16), Min Frame (24), Max Frame (24) */
          /* Sample Rate (20), Channels (3), Bits (5), Total Samples (36) */
          /* Offset in bits: 16+16+24+24 = 80 bits = 10 bytes */
          /* We need bytes 10-18 approx */

          int bit_off = 80;
          uint64_t sample_rate = get_bits(data, &bit_off, 20);
          uint64_t channels = get_bits(data, &bit_off, 3) + 1; /* 000 = 1 ch */
          get_bits(data, &bit_off, 5); /* bits per sample */
          uint64_t total_samples = get_bits(data, &bit_off, 36);

          if (sample_rate > 0) {
            out_track->duration = (float)total_samples / (float)sample_rate;
            log_message("INFO", "parse_flac: Calculated duration");
          }
        }
        free(data);
      } else {
        fseek(f, length, SEEK_CUR);
      }
    } else if (type == 4) { /* VORBIS_COMMENT */
      long start_pos = ftell(f);
      unsigned char tmp[4];

      /* Vendor length */
      if (fread(tmp, 1, 4, f) == 4) {
        int vendor_len = read_le32u(tmp); /* Little endian in Vorbis */
        fseek(f, vendor_len, SEEK_CUR);   /* Skip vendor string */

        /* User comment list length */
        if (fread(tmp, 1, 4, f) == 4) {
          int num_comments = read_le32u(tmp);

          for (int i = 0; i < num_comments; i++) {
            if (fread(tmp, 1, 4, f) != 4)
              break;
            int comment_len = read_le32u(tmp);
            if (comment_len < 0 || comment_len > 1024 * 10) {
              /* Safety break */
              break; /* invalid len */
            }

            char *comment = (char *)malloc(comment_len + 1);
            if (comment) {
              if (fread(comment, 1, comment_len, f) == (size_t)comment_len) {
                comment[comment_len] = '\0';

                /* Parse FIELD=VALUE */
                char *eq = strchr(comment, '=');
                if (eq) {
                  *eq = '\0';
                  const char *val = eq + 1;
                  if (strcasecmp(comment, "TITLE") == 0) {
                    strncpy(out_track->title, val, MAX_SONG_TITLE - 1);
                    found_info = true;
                  } else if (strcasecmp(comment, "ARTIST") == 0) {
                    strncpy(out_artist, val, MAX_SONG_TITLE - 1);
                  } else if (strcasecmp(comment, "ALBUM") == 0) {
                    strncpy(out_album, val, 255);
                  } else if (strcasecmp(comment, "DATE") == 0 ||
                             strcasecmp(comment, "YEAR") == 0) {
                    out_track->year = atoi(val);
                  } else if (strcasecmp(comment, "TRACKNUMBER") == 0) {
                    out_track->track_number = atoi(val);
                  } else if (strcasecmp(comment, "GENRE") == 0) {
                    strncpy(out_track->genre, val, 63);
                  }
                }
              }
              free(comment);
            } else {
              fseek(f, comment_len, SEEK_CUR);
            }
          }
        }
      }
      /* Ensure we skip exactly length bytes from start of block content */
      fseek(f, start_pos + length, SEEK_SET);

    } else {
      /* Skip other blocks */
      fseek(f, length, SEEK_CUR);
    }
  }

  return (found_info || out_artist[0] != '\0');
}

/* =========================================================
 *              MP3 Duration Estimation
 * ========================================================= */

static float estimate_mp3_duration(FILE *f, long file_size) {
  /* Simple VBR/CBR estimator */
  /* 1. Look for Xing/Info header in first few frames */
  /* 2. Else use average bitrate of first frame */

  fseek(f, 0, SEEK_SET);
  unsigned char buf[4096]; /* Read a chunk to scan for header */
  size_t read_bytes = fread(buf, 1, sizeof(buf), f);

  if (read_bytes < 128)
    return 0.0f;

  /* Scan for frame sync 0xFFE0 (11 bits set) */
  /* This is a very basic parser. */
  int pos = 0;
  bool found_frame = false;
  int bitrate = 0;
  int sample_rate = 0;
  int padding = 0;
  int mpeg_ver = 0; /* 3=MPEG1, 2=MPEG2 */
  int layer = 0;

  for (int i = 0; i < (int)read_bytes - 4; i++) {
    if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
      /* Found POTENTIAL sync */
      unsigned char b1 = buf[i];
      unsigned char b2 = buf[i + 1];
      unsigned char b3 = buf[i + 2];
      /* unsigned char b4 = buf[i+3]; */

      mpeg_ver = (b2 >> 3) & 0x03; /* 11=v1, 10=v2, 00=v2.5 */
      layer = (b2 >> 1) & 0x03;    /* 01=L3, 10=L2, 11=L1 */

      if (mpeg_ver == 1)
        continue; /* reserved */
      if (layer == 0)
        continue; /* reserved */

      int bitrate_idx = (b3 >> 4) & 0x0F;
      int samprate_idx = (b3 >> 2) & 0x03;
      padding = (b3 >> 1) & 0x01;

      if (bitrate_idx == 0 || bitrate_idx == 15)
        continue; /* free/bad */
      if (samprate_idx == 3)
        continue; /* res */

      /* Lookup bitrate (MPEG1 Layer3) - Simplified for common MP3s */
      /* Real table is complex. Let's approximate or use standard set for L3 */

      const int br_v1_l3[] = {0,   32,  40,  48,  56,  64,  80, 96,
                              112, 128, 160, 192, 224, 256, 320};
      const int br_v2_l3[] = {0,  8,  16, 24,  32,  40,  48, 56,
                              64, 80, 96, 112, 128, 144, 160};

      if ((mpeg_ver & 0x03) == 0x03) { /* V1 */
        if (layer == 1)
          bitrate = br_v1_l3[bitrate_idx]; /* Using l3 table for now, beware */
        else
          bitrate = br_v1_l3[bitrate_idx]; /* Assuming L3 most common */
      } else {
        bitrate = br_v2_l3[bitrate_idx];
      }

      const int sr_table[3] = {44100, 48000, 32000};
      const int sr_table_v2[3] = {22050, 24000, 16000};
      /* v2.5 is lower */

      if ((mpeg_ver & 0x03) == 0x03)
        sample_rate = sr_table[samprate_idx];
      else
        sample_rate = sr_table_v2[samprate_idx];

      found_frame = true;
      pos = i;
      break;
    }
  }

  if (found_frame && bitrate > 0) {
    /* Check for Xing/Info in this frame */
    /* Offset for L3: Side info is 17 bytes (mono) or 32 bytes (stereo) */
    /* Then checking for "Xing" or "Info" */

    // Simplification: Just Search for "Xing" or "Info" in the buffer
    // They are usually within the first 100 bytes of the frame

    for (int j = pos; j < pos + 100 && j < (int)read_bytes - 4; j++) {
      if (memcmp(buf + j, "Xing", 4) == 0 || memcmp(buf + j, "Info", 4) == 0) {
        /* Found VBR header */
        unsigned char flags_buf[4];
        if (j + 8 < (int)read_bytes) {
          int flags = read_be32(buf + j + 4);
          // If frames flag is set (0x0001) check frames
          if (flags & 0x0001) {
            if (j + 12 < (int)read_bytes) {
              int frames = read_be32(buf + j + 8);

              /* Samples per frame. MPEG1 L3 = 1152. MPEG2 L3 = 576 */
              int samples_per_frame = ((mpeg_ver & 0x03) == 0x03) ? 1152 : 576;

              if (sample_rate > 0)
                return (float)frames * samples_per_frame / (float)sample_rate;
            }
          }
        }
      }
    }

    /* Fallback: CBR */
    /* Duration = (FileSize * 8) / (Bitrate * 1000) */
    return (float)file_size * 8.0f / (float)(bitrate * 1000);
  }

  return 0.0f;
}

bool get_metadata(const char *path, Track *out_track, char *out_artist,
                  char *out_album) {
  char msg[1024];
  snprintf(msg, sizeof(msg),
           "get_metadata: Starting metadata read for path '%s'", path);
  log_message("INFO", msg);
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(msg, sizeof(msg), "get_metadata: Failed to open file: '%s'", path);
    log_message("INFO", msg);
    return false;
  }

  /* Get file size for WAV duration calc */
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  /* Initialize outputs to empty */
  out_track->title[0] = '\0';
  out_artist[0] = '\0';
  out_album[0] = '\0';
  out_track->year = 0;
  out_track->track_number = 0;
  out_track->disc_number = 0;
  out_track->duration = 0.0f;

  unsigned char header[10];
  if (fread(header, 1, 10, f) != 10) {
    fclose(f);
    return false;
  }

  /* Check ID3v2 identifier */
  if (memcmp(header, "ID3", 3) != 0) {
    /* Not ID3v2, try M4A/MP4 format */
    bool is_m4a = (memcmp(header, "\x00\x00\x00", 3) == 0 || header[4] == 'f');
    if (is_m4a) {
      log_message("INFO", "get_metadata: Trying M4A/MP4 format");
      bool m4a_result = parse_m4a_metadata(f, out_track, out_artist, out_album);
      /* Helper re-reads from start, so we pass F */
      /* But parse_m4a_metadata seeks around. Let's call duration helper
       * similarly. */
      out_track->duration = get_m4a_duration(f);
      fclose(f);
      return m4a_result;
    }

    /* Check for RIFF/WAVE */
    if (memcmp(header, "RIFF", 4) == 0) {
      log_message("INFO", "get_metadata: Trying WAV format");
      out_track->duration = get_wav_duration(f, (uint32_t)file_size);

      bool wav_result =
          parse_wav_metadata(f, out_track->title, out_artist, MAX_SONG_TITLE);
      fclose(f);
      return wav_result;
    }

    /* Check for FLAC */
    if (memcmp(header, "fLaC", 4) == 0) {
      log_message("INFO", "get_metadata: Trying FLAC format");
      bool res = parse_flac_metadata(f, out_track, out_artist, out_album);
      fclose(f);
      return res;
    }

    /* Fallback: If it's an MP3 or other audio file without tags, just use the
     * filename */
    log_message(
        "INFO",
        "get_metadata: No supported tags found, falling back to filename");
    const char *filename = strrchr(path, '/');
    if (filename) {
      filename++; /* skip the slash */
    } else {
      filename = path;
    }
    strncpy(out_track->title, filename, MAX_SONG_TITLE - 1);
    out_track->title[MAX_SONG_TITLE - 1] = '\0';

    /* Optional: estimate MP3 duration if it looks like an MP3 file extension */
    if (strstr(filename, ".mp3") || strstr(filename, ".MP3")) {
      out_track->duration = estimate_mp3_duration(f, file_size);
    }

    fclose(f);
    return true; /* At least we got the filename */
  }

  int version = header[3];
  if (version < 3) {
    fclose(f);
    return false;
  }

  int tag_size = decode_syncsafe(&header[6]);
  if (tag_size > 512 * 1024)
    tag_size = 512 * 1024;

  unsigned char *tag_data = (unsigned char *)malloc(tag_size);
  if (!tag_data) {
    fclose(f);
    return false;
  }

  if (fread(tag_data, 1, tag_size, f) != (size_t)tag_size) {
    free(tag_data);
    fclose(f);
    return false;
  }

  /* MP3 Duration: If not found in tags later, we will use estimate */
  /* We can't easily check tags here without parsing loop, so we init duration
     to 0 and if it stays 0 after parsing, we estimate. */

  fclose(f);

  int pos = 0;
  bool found_title = false;

  while (pos < tag_size - 10) {
    char frame_id[5] = {0};
    memcpy(frame_id, &tag_data[pos], 4);
    if (frame_id[0] == 0)
      break;

    int frame_size = decode_int(&tag_data[pos + 4]);
    if (frame_size < 2 || pos + 10 + frame_size > tag_size)
      break;

    char *content = (char *)&tag_data[pos + 10];
    int enc = content[0];

    if (strcmp(frame_id, "TIT2") == 0) {
      /* ... (unchanged) ... */
      if (enc == 0 || enc == 3) {
        int copy_len = (frame_size - 1 < MAX_SONG_TITLE - 1)
                           ? frame_size - 1
                           : MAX_SONG_TITLE - 1;
        memcpy(out_track->title, content + 1, copy_len);
        out_track->title[copy_len] = '\0';
        found_title = true;
      }
    } else if (strcmp(frame_id, "TPE1") == 0) {
      if (enc == 0 || enc == 3) {
        int copy_len = (frame_size - 1 < MAX_SONG_TITLE - 1)
                           ? frame_size - 1
                           : MAX_SONG_TITLE - 1;
        memcpy(out_artist, content + 1, copy_len);
        out_artist[copy_len] = '\0';
      }
    } else if (strcmp(frame_id, "TALB") == 0) {
      if (enc == 0 || enc == 3) {
        int copy_len = (frame_size - 1 < 255) ? frame_size - 1 : 255;
        memcpy(out_album, content + 1, copy_len);
        out_album[copy_len] = '\0';
      }
    } else if (strcmp(frame_id, "TYER") == 0 || strcmp(frame_id, "TDRC") == 0) {
      if (enc == 0 || enc == 3) {
        out_track->year = atoi(content + 1);
      }
    } else if (strcmp(frame_id, "TRCK") == 0) {
      if (enc == 0 || enc == 3) {
        out_track->track_number = atoi(content + 1);
      }
    } else if (strcmp(frame_id, "TLEN") == 0) {
      /* Width of duration in milliseconds */
      if (enc == 0 || enc == 3) {
        int ms = atoi(content + 1);
        if (ms > 0) {
          out_track->duration = (float)ms / 1000.0f;
          log_message("INFO", "get_metadata: Found duration (TLEN)");
        }
      }
    } else if (strcmp(frame_id, "TCON") == 0) {
      if (enc == 0 || enc == 3) {
        int copy_len = (frame_size - 1 < 63) ? frame_size - 1 : 63;
        memcpy(out_track->genre, content + 1, copy_len);
        out_track->genre[copy_len] = '\0';
      }
    }

    pos += 10 + frame_size;
  }

  free(tag_data);

  /* Fallback for MP3 if duration <= 0 */
  if (out_track->duration <= 0.001f) {
    FILE *f2 = fopen(path, "rb");
    if (f2) {
      out_track->duration = estimate_mp3_duration(f2, file_size);
      fclose(f2);
    }
  }

  return (found_title || out_artist[0] != '\0' || out_album[0] != '\0');
}
