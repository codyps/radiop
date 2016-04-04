/*
 * Bytes that change every reboot:
 *	1033: 0x31, 0x9b
 *	1225: 0x31, 0x9b
 *		[I've observed these being equal & changing together]
 *
 *
 * 840, 1033, 1224, 32584: changes with a reboot to a different value
 *
 *
 * With change to 144005:
 *
 * 872,896,1064 = changes whith freq change
 * 1088 = current frequency (band 4)
 *
 * With change to 144050:
 *
 * 872,1064 = change
 * 896 = frequency
 * 1088 = frequency
 *
 * With change to 144500:
 *	[noticed some more freq usages, using more specific offsets]
 *
 * 870, 898, 1090, 1062 = frequency
 *	[seems like there might be 4 contexts where the VFO frequncy is needed]
 *
 *
 * With change to 150000 and save as mem 1:
 *
 * 712: 0x8e to 0xce
 * 862: 0x00 to 0x05
 * 870...871: 1445 to 1500
 * 898...899: 1445 to 1500
 * 992: 00 to 01
 * 1033: 0x9b to 0x1c
 * 1054: 00 to 05
 * 1056: 00 to 03
 * 1062: 1445 to 1500
 * 1090: 1445 to 1500
 * 1184: 00 to 01
 * 1225: 9b to 1c
 * 3445: 1440 to 1445
 * 9290: 00 to 05
 * 9291: 00 to 03
 * 9292: 1440 to 1500
 * 32586: 6b to 7d
 *
 * With change to V/M 1 (150000):
 *
 * 849: 01 to 02
 * 910: 03 to 06
 * 911: 01 to 02
 * 914: ffff to 0000
 * 1033: 1c to 23
 * 1041: 01 to 02
 * 1102: 03 to 06
 * 1103: 01 to 02
 * 1106: ffff to 0000
 * 1225:  1c to 23
 * 3444: 1445 to 1500
 * 3461: 18 to 10
 * 32586: 7d to 55
 *
 * Switch to VFO:
 *
 * 849: 02 to 01
 * 889: 18 to 10
 * 1033: 23 to 1a
 * 1041: 02 to 01
 * 1081: 18 to 10
 * 1225: 23 to 1a
 * 32586: 55 to 31
 *
 * Switch back to V/M 1 (150000):
 *
 * 849: 01 to 02
 * 889: 10 to 18
 * 1033: 1a to 23
 * 1041: 01 to 02
 * 1081: 10 to 18
 * 1225: 1a to 23
 * 32586: 31 to 55

 */
