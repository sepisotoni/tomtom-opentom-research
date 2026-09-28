#ifndef TTFACE_PACKAGE_H
#define TTFACE_PACKAGE_H

#define TTFACE_MANIFEST_MAX 16384U

/*
 * Extract the two version-1 package entries into a new staging directory.
 * Only manifest.json and assets/bg.rgb565 are accepted; paths from the ZIP
 * are never used as filesystem paths. The caller must pass a directory that
 * does not already exist and must ignore it if extraction fails.
 */
int ttface_unpack_archive(const char *archive_path,
                          const char *staging_directory);

#endif
