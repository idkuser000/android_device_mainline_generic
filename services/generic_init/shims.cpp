/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

extern "C" {
int getfilecon(const char *path, char ** con) { return -1; }
int setfscreatecon(const char * context) { return 0; }
}
