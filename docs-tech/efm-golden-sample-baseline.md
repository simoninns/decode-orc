# Audio
## Input file
roger_rabbit.efm

### EFM SHA256

[sdi@titan:~/raid/decodes/efm_tests/audio]$ sha256sum roger_rabbit.efm 
95658ab7e6558e78a716c4d086e7617550c254439a1e23a4d3cd5fd557939be0  roger_rabbit.efm

## EFM to F2
[sdi@titan:~/raid/decodes/efm_tests/audio]$ time efm-decoder-f2 roger_rabbit.efm roger_rabbit.f2
Info: Beginning EFM decoding of "roger_rabbit.efm"
Info: Progress: 5 %
Info: Progress: 10 %
Info: Progress: 15 %
Info: Progress: 20 %
Info: Progress: 25 %
Info: Progress: 30 %
Info: Progress: 35 %
Info: Progress: 40 %
Info: Progress: 45 %
Info: Progress: 50 %
Info: Progress: 55 %
Info: Progress: 60 %
Info: Progress: 65 %
Info: Progress: 70 %
Info: Progress: 75 %
Info: Progress: 80 %
Info: Progress: 85 %
Info: Progress: 90 %
Info: Progress: 95 %
Info: Progress: 100 %
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: T-values to Channel Frame statistics:
Info:   T-Values:
Info:     Consumed: 1302496148
Info:     Discarded: 13
Info:   Channel frames:
Info:     Total: 10783623
Info:     588 bits: 10783413
Info:     >588 bits: 105
Info:     <588 bits: 105
Info:   Sync headers:
Info:     Good syncs: 10782105
Info:     Overshoots: 756
Info:     Undershoots: 5
Info:     Guessed: 757
Info: 
Info: Channel to F3 Frame statistics:
Info:   Channel Frames:
Info:     Total: 10783623
Info:     Good: 10783413
Info:     Undershoot: 105
Info:     Overshoot: 105
Info:   EFM symbols:
Info:     Valid: 345075633
Info:     Invalid: 298
Info:   Subcode symbols:
Info:     Valid: 10783620
Info:     Invalid: 3
Info: 
Info: F3 Frame to F2 Section statistics:
Info:   F3 Frames:
Info:     Input frames: 10783623
Info:     Good sync0 frames: 110028
Info:     Missing sync0 frames: 8
Info:     Undershoot sync0 frames: 8
Info:     Overshoot sync0 frames: 0
Info:     Lost sync: 0
Info:   Frame loss:
Info:     Presync discarded F3 frames: 0
Info:     Discarded F3 frames: 0
Info:     Padded F3 frames: 8
Info: 
Info: F2 Section Metadata Correction statistics:
Info:   F2 Sections:
Info:     Total: 110034 (10783332 F2)
Info:     Corrected: 34
Info:     Uncorrectable: 0
Info:     Pre-Leadin: 2
Info:     Missing: 0
Info:     Padding: 0
Info:     Out of order: 0
Info:   QMode Sections:
Info:     QMode 1 (CD Data): 0
Info:     QMode 2 (Catalogue No.): 0
Info:     QMode 3 (ISO 3901 ISRC): 0
Info:     QMode 4 (LD Data): 110034
Info:   Absolute Time:
Info:     Start time: 00:00:00
Info:     End time: 24:27:08
Info:     Duration: 24:27:08
Info:   Track 1:
Info:     Start time: 00:00:00
Info:     End time: 07:38:44
Info:     Duration: 07:38:44
Info:   Track 2:
Info:     Start time: 00:00:00
Info:     End time: 07:28:38
Info:     Duration: 07:28:38
Info:   Track 3:
Info:     Start time: 00:00:00
Info:     End time: 09:17:71
Info:     Duration: 09:17:71
Info: 
Info: Decoder processing summary (general):
Info:   Channel to F3 processing time: 12190 ms
Info:   F3 to F2 section processing time: 1947 ms
Info:   F2 correction processing time: 46 ms
Info:   Total processing time: 14184 ms (14.18 seconds)
Info: 

real	0m29.478s
user	0m26.906s
sys	0m0.880s

### F2 SHA256

[sdi@titan:~/raid/decodes/efm_tests/audio]$ sha256sum roger_rabbit.f2
6021028ce6fc368cc578d9b99065401b8cecc2262f7bf3cbd4d76b8155b29759  roger_rabbit.f2

## F2 to D24
[sdi@titan:~/raid/decodes/efm_tests/audio]$ time efm-decoder-d24 roger_rabbit.f2 roger_rabbit.d24
Info: Beginning EFM decoding of "roger_rabbit.f2"
Info: Decoding F2 Section 0 of 110034 (0.00%)
Info: Decoding F2 Section 1000 of 110034 (0.91%)
Info: Decoding F2 Section 2000 of 110034 (1.82%)
Info: Decoding F2 Section 3000 of 110034 (2.73%)
Info: Decoding F2 Section 4000 of 110034 (3.64%)
Info: Decoding F2 Section 5000 of 110034 (4.54%)
Info: Decoding F2 Section 6000 of 110034 (5.45%)
Info: Decoding F2 Section 7000 of 110034 (6.36%)
Info: Decoding F2 Section 8000 of 110034 (7.27%)
Info: Decoding F2 Section 9000 of 110034 (8.18%)
Info: Decoding F2 Section 10000 of 110034 (9.09%)
Info: Decoding F2 Section 11000 of 110034 (10.00%)
Info: Decoding F2 Section 12000 of 110034 (10.91%)
Info: Decoding F2 Section 13000 of 110034 (11.81%)
Info: Decoding F2 Section 14000 of 110034 (12.72%)
Info: Decoding F2 Section 15000 of 110034 (13.63%)
Info: Decoding F2 Section 16000 of 110034 (14.54%)
Info: Decoding F2 Section 17000 of 110034 (15.45%)
Info: Decoding F2 Section 18000 of 110034 (16.36%)
Info: Decoding F2 Section 19000 of 110034 (17.27%)
Info: Decoding F2 Section 20000 of 110034 (18.18%)
Info: Decoding F2 Section 21000 of 110034 (19.09%)
Info: Decoding F2 Section 22000 of 110034 (19.99%)
Info: Decoding F2 Section 23000 of 110034 (20.90%)
Info: Decoding F2 Section 24000 of 110034 (21.81%)
Info: Decoding F2 Section 25000 of 110034 (22.72%)
Info: Decoding F2 Section 26000 of 110034 (23.63%)
Info: Decoding F2 Section 27000 of 110034 (24.54%)
Info: Decoding F2 Section 28000 of 110034 (25.45%)
Info: Decoding F2 Section 29000 of 110034 (26.36%)
Info: Decoding F2 Section 30000 of 110034 (27.26%)
Info: Decoding F2 Section 31000 of 110034 (28.17%)
Info: Decoding F2 Section 32000 of 110034 (29.08%)
Info: Decoding F2 Section 33000 of 110034 (29.99%)
Info: Decoding F2 Section 34000 of 110034 (30.90%)
Info: Decoding F2 Section 35000 of 110034 (31.81%)
Info: Decoding F2 Section 36000 of 110034 (32.72%)
Info: Decoding F2 Section 37000 of 110034 (33.63%)
Info: Decoding F2 Section 38000 of 110034 (34.53%)
Info: Decoding F2 Section 39000 of 110034 (35.44%)
Info: Decoding F2 Section 40000 of 110034 (36.35%)
Info: Decoding F2 Section 41000 of 110034 (37.26%)
Info: Decoding F2 Section 42000 of 110034 (38.17%)
Info: Decoding F2 Section 43000 of 110034 (39.08%)
Info: Decoding F2 Section 44000 of 110034 (39.99%)
Info: Decoding F2 Section 45000 of 110034 (40.90%)
Info: Decoding F2 Section 46000 of 110034 (41.81%)
Info: Decoding F2 Section 47000 of 110034 (42.71%)
Info: Decoding F2 Section 48000 of 110034 (43.62%)
Info: Decoding F2 Section 49000 of 110034 (44.53%)
Info: Decoding F2 Section 50000 of 110034 (45.44%)
Info: Decoding F2 Section 51000 of 110034 (46.35%)
Info: Decoding F2 Section 52000 of 110034 (47.26%)
Info: Decoding F2 Section 53000 of 110034 (48.17%)
Info: Decoding F2 Section 54000 of 110034 (49.08%)
Info: Decoding F2 Section 55000 of 110034 (49.98%)
Info: Decoding F2 Section 56000 of 110034 (50.89%)
Info: Decoding F2 Section 57000 of 110034 (51.80%)
Info: Decoding F2 Section 58000 of 110034 (52.71%)
Info: Decoding F2 Section 59000 of 110034 (53.62%)
Info: Decoding F2 Section 60000 of 110034 (54.53%)
Info: Decoding F2 Section 61000 of 110034 (55.44%)
Info: Decoding F2 Section 62000 of 110034 (56.35%)
Info: Decoding F2 Section 63000 of 110034 (57.26%)
Info: Decoding F2 Section 64000 of 110034 (58.16%)
Info: Decoding F2 Section 65000 of 110034 (59.07%)
Info: Decoding F2 Section 66000 of 110034 (59.98%)
Info: Decoding F2 Section 67000 of 110034 (60.89%)
Info: Decoding F2 Section 68000 of 110034 (61.80%)
Info: Decoding F2 Section 69000 of 110034 (62.71%)
Info: Decoding F2 Section 70000 of 110034 (63.62%)
Info: Decoding F2 Section 71000 of 110034 (64.53%)
Info: Decoding F2 Section 72000 of 110034 (65.43%)
Info: Decoding F2 Section 73000 of 110034 (66.34%)
Info: Decoding F2 Section 74000 of 110034 (67.25%)
Info: Decoding F2 Section 75000 of 110034 (68.16%)
Info: Decoding F2 Section 76000 of 110034 (69.07%)
Info: Decoding F2 Section 77000 of 110034 (69.98%)
Info: Decoding F2 Section 78000 of 110034 (70.89%)
Info: Decoding F2 Section 79000 of 110034 (71.80%)
Info: Decoding F2 Section 80000 of 110034 (72.70%)
Info: Decoding F2 Section 81000 of 110034 (73.61%)
Info: Decoding F2 Section 82000 of 110034 (74.52%)
Info: Decoding F2 Section 83000 of 110034 (75.43%)
Info: Decoding F2 Section 84000 of 110034 (76.34%)
Info: Decoding F2 Section 85000 of 110034 (77.25%)
Info: Decoding F2 Section 86000 of 110034 (78.16%)
Info: Decoding F2 Section 87000 of 110034 (79.07%)
Info: Decoding F2 Section 88000 of 110034 (79.98%)
Info: Decoding F2 Section 89000 of 110034 (80.88%)
Info: Decoding F2 Section 90000 of 110034 (81.79%)
Info: Decoding F2 Section 91000 of 110034 (82.70%)
Info: Decoding F2 Section 92000 of 110034 (83.61%)
Info: Decoding F2 Section 93000 of 110034 (84.52%)
Info: Decoding F2 Section 94000 of 110034 (85.43%)
Info: Decoding F2 Section 95000 of 110034 (86.34%)
Info: Decoding F2 Section 96000 of 110034 (87.25%)
Info: Decoding F2 Section 97000 of 110034 (88.15%)
Info: Decoding F2 Section 98000 of 110034 (89.06%)
Info: Decoding F2 Section 99000 of 110034 (89.97%)
Info: Decoding F2 Section 100000 of 110034 (90.88%)
Info: Decoding F2 Section 101000 of 110034 (91.79%)
Info: Decoding F2 Section 102000 of 110034 (92.70%)
Info: Decoding F2 Section 103000 of 110034 (93.61%)
Info: Decoding F2 Section 104000 of 110034 (94.52%)
Info: Decoding F2 Section 105000 of 110034 (95.43%)
Info: Decoding F2 Section 106000 of 110034 (96.33%)
Info: Decoding F2 Section 107000 of 110034 (97.24%)
Info: Decoding F2 Section 108000 of 110034 (98.15%)
Info: Decoding F2 Section 109000 of 110034 (99.06%)
Info: Decoding F2 Section 110000 of 110034 (99.97%)
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: F2 Section to F1 Section statistics:
Info:   Input F2 Frames:
Info:     Valid frames: 10783030
Info:     Corrupt frames: 302 frames containing 559 byte errors
Info:     Delay line lost frames: 111
Info:     Continuity errors: 0
Info:   Output F1 Frames (after CIRC):
Info:     Valid frames: 10783221
Info:     Invalid frames due to padding: 0
Info:     Invalid frames without padding: 0
Info:     Invalid frames (total): 0
Info:     Output byte errors: 0
Info:   C1 decoder:
Info:     Valid C1s: 10763414
Info:     Fixed C1s: 19684
Info:     Error C1s: 233
Info:   C2 decoder:
Info:     Valid C2s: 10779529
Info:     Fixed C2s: 3694
Info:     Error C2s: 0
Info: 
Info: F1 Section to Data24 Section statistics:
Info:   Frames:
Info:     Total F1 frames: 10783332
Info:     Error-free F1 frames: 10783332
Info:     F1 frames containing errors: 0
Info:     Padded F1 frames: 0
Info:     Unpadded F1 frames: 10783332
Info:   Data:
Info:     Total MBytes: 246.81
Info:     Valid MBytes: 246.81
Info:     Corrupt MBytes: 0.00
Info:     Padded MBytes: 0.00
Info:     Data loss: 0.000%
Info: 
Info: Decoder processing summary (general):
Info:   F2 to F1 processing time: 29448 ms
Info:   F1 to Data24 processing time: 3063 ms
Info:   Total processing time: 32512 ms (32.51 seconds)
Info: 
Info: Encoding complete

real	0m41.445s
user	0m40.142s
sys	0m0.519s

### D24 SHA256

[sdi@titan:~/raid/decodes/efm_tests/audio]$ sha256sum roger_rabbit.d24
e64c31cf190a46a4079fd8a6c66bee0b26449cb938380f47a4783c5fbadc0a36  roger_rabbit.d24

## D24 to WAV
[sdi@titan:~/raid/decodes/efm_tests/audio]$ time efm-decoder-audio roger_rabbit.d24 roger_rabbit.wav
Info: Beginning EFM decoding of "roger_rabbit.d24"
Info: Decoding Data24 Section 0 of 110034 (0.00%)
Info: Decoding Data24 Section 500 of 110034 (0.45%)
Info: Decoding Data24 Section 1000 of 110034 (0.91%)
Info: Decoding Data24 Section 1500 of 110034 (1.36%)
Info: Decoding Data24 Section 2000 of 110034 (1.82%)
Info: Decoding Data24 Section 2500 of 110034 (2.27%)
Info: Decoding Data24 Section 3000 of 110034 (2.73%)
Info: Decoding Data24 Section 3500 of 110034 (3.18%)
Info: Decoding Data24 Section 4000 of 110034 (3.64%)
Info: Decoding Data24 Section 4500 of 110034 (4.09%)
Info: Decoding Data24 Section 5000 of 110034 (4.54%)
Info: Decoding Data24 Section 5500 of 110034 (5.00%)
Info: Decoding Data24 Section 6000 of 110034 (5.45%)
Info: Decoding Data24 Section 6500 of 110034 (5.91%)
Info: Decoding Data24 Section 7000 of 110034 (6.36%)
Info: Decoding Data24 Section 7500 of 110034 (6.82%)
Info: Decoding Data24 Section 8000 of 110034 (7.27%)
Info: Decoding Data24 Section 8500 of 110034 (7.72%)
Info: Decoding Data24 Section 9000 of 110034 (8.18%)
Info: Decoding Data24 Section 9500 of 110034 (8.63%)
Info: Decoding Data24 Section 10000 of 110034 (9.09%)
Info: Decoding Data24 Section 10500 of 110034 (9.54%)
Info: Decoding Data24 Section 11000 of 110034 (10.00%)
Info: Decoding Data24 Section 11500 of 110034 (10.45%)
Info: Decoding Data24 Section 12000 of 110034 (10.91%)
Info: Decoding Data24 Section 12500 of 110034 (11.36%)
Info: Decoding Data24 Section 13000 of 110034 (11.81%)
Info: Decoding Data24 Section 13500 of 110034 (12.27%)
Info: Decoding Data24 Section 14000 of 110034 (12.72%)
Info: Decoding Data24 Section 14500 of 110034 (13.18%)
Info: Decoding Data24 Section 15000 of 110034 (13.63%)
Info: Decoding Data24 Section 15500 of 110034 (14.09%)
Info: Decoding Data24 Section 16000 of 110034 (14.54%)
Info: Decoding Data24 Section 16500 of 110034 (15.00%)
Info: Decoding Data24 Section 17000 of 110034 (15.45%)
Info: Decoding Data24 Section 17500 of 110034 (15.90%)
Info: Decoding Data24 Section 18000 of 110034 (16.36%)
Info: Decoding Data24 Section 18500 of 110034 (16.81%)
Info: Decoding Data24 Section 19000 of 110034 (17.27%)
Info: Decoding Data24 Section 19500 of 110034 (17.72%)
Info: Decoding Data24 Section 20000 of 110034 (18.18%)
Info: Decoding Data24 Section 20500 of 110034 (18.63%)
Info: Decoding Data24 Section 21000 of 110034 (19.09%)
Info: Decoding Data24 Section 21500 of 110034 (19.54%)
Info: Decoding Data24 Section 22000 of 110034 (19.99%)
Info: Decoding Data24 Section 22500 of 110034 (20.45%)
Info: Decoding Data24 Section 23000 of 110034 (20.90%)
Info: Decoding Data24 Section 23500 of 110034 (21.36%)
Info: Decoding Data24 Section 24000 of 110034 (21.81%)
Info: Decoding Data24 Section 24500 of 110034 (22.27%)
Info: Decoding Data24 Section 25000 of 110034 (22.72%)
Info: Decoding Data24 Section 25500 of 110034 (23.17%)
Info: Decoding Data24 Section 26000 of 110034 (23.63%)
Info: Decoding Data24 Section 26500 of 110034 (24.08%)
Info: Decoding Data24 Section 27000 of 110034 (24.54%)
Info: Decoding Data24 Section 27500 of 110034 (24.99%)
Info: Decoding Data24 Section 28000 of 110034 (25.45%)
Info: Decoding Data24 Section 28500 of 110034 (25.90%)
Info: Decoding Data24 Section 29000 of 110034 (26.36%)
Info: Decoding Data24 Section 29500 of 110034 (26.81%)
Info: Decoding Data24 Section 30000 of 110034 (27.26%)
Info: Decoding Data24 Section 30500 of 110034 (27.72%)
Info: Decoding Data24 Section 31000 of 110034 (28.17%)
Info: Decoding Data24 Section 31500 of 110034 (28.63%)
Info: Decoding Data24 Section 32000 of 110034 (29.08%)
Info: Decoding Data24 Section 32500 of 110034 (29.54%)
Info: Decoding Data24 Section 33000 of 110034 (29.99%)
Info: Decoding Data24 Section 33500 of 110034 (30.45%)
Info: Decoding Data24 Section 34000 of 110034 (30.90%)
Info: Decoding Data24 Section 34500 of 110034 (31.35%)
Info: Decoding Data24 Section 35000 of 110034 (31.81%)
Info: Decoding Data24 Section 35500 of 110034 (32.26%)
Info: Decoding Data24 Section 36000 of 110034 (32.72%)
Info: Decoding Data24 Section 36500 of 110034 (33.17%)
Info: Decoding Data24 Section 37000 of 110034 (33.63%)
Info: Decoding Data24 Section 37500 of 110034 (34.08%)
Info: Decoding Data24 Section 38000 of 110034 (34.53%)
Info: Decoding Data24 Section 38500 of 110034 (34.99%)
Info: Decoding Data24 Section 39000 of 110034 (35.44%)
Info: Decoding Data24 Section 39500 of 110034 (35.90%)
Info: Decoding Data24 Section 40000 of 110034 (36.35%)
Info: Decoding Data24 Section 40500 of 110034 (36.81%)
Info: Decoding Data24 Section 41000 of 110034 (37.26%)
Info: Decoding Data24 Section 41500 of 110034 (37.72%)
Info: Decoding Data24 Section 42000 of 110034 (38.17%)
Info: Decoding Data24 Section 42500 of 110034 (38.62%)
Info: Decoding Data24 Section 43000 of 110034 (39.08%)
Info: Decoding Data24 Section 43500 of 110034 (39.53%)
Info: Decoding Data24 Section 44000 of 110034 (39.99%)
Info: Decoding Data24 Section 44500 of 110034 (40.44%)
Info: Decoding Data24 Section 45000 of 110034 (40.90%)
Info: Decoding Data24 Section 45500 of 110034 (41.35%)
Info: Decoding Data24 Section 46000 of 110034 (41.81%)
Info: Decoding Data24 Section 46500 of 110034 (42.26%)
Info: Decoding Data24 Section 47000 of 110034 (42.71%)
Info: Decoding Data24 Section 47500 of 110034 (43.17%)
Info: Decoding Data24 Section 48000 of 110034 (43.62%)
Info: Decoding Data24 Section 48500 of 110034 (44.08%)
Info: Decoding Data24 Section 49000 of 110034 (44.53%)
Info: Decoding Data24 Section 49500 of 110034 (44.99%)
Info: Decoding Data24 Section 50000 of 110034 (45.44%)
Info: Decoding Data24 Section 50500 of 110034 (45.89%)
Info: Decoding Data24 Section 51000 of 110034 (46.35%)
Info: Decoding Data24 Section 51500 of 110034 (46.80%)
Info: Decoding Data24 Section 52000 of 110034 (47.26%)
Info: Decoding Data24 Section 52500 of 110034 (47.71%)
Info: Decoding Data24 Section 53000 of 110034 (48.17%)
Info: Decoding Data24 Section 53500 of 110034 (48.62%)
Info: Decoding Data24 Section 54000 of 110034 (49.08%)
Info: Decoding Data24 Section 54500 of 110034 (49.53%)
Info: Decoding Data24 Section 55000 of 110034 (49.98%)
Info: Decoding Data24 Section 55500 of 110034 (50.44%)
Info: Decoding Data24 Section 56000 of 110034 (50.89%)
Info: Decoding Data24 Section 56500 of 110034 (51.35%)
Info: Decoding Data24 Section 57000 of 110034 (51.80%)
Info: Decoding Data24 Section 57500 of 110034 (52.26%)
Info: Decoding Data24 Section 58000 of 110034 (52.71%)
Info: Decoding Data24 Section 58500 of 110034 (53.17%)
Info: Decoding Data24 Section 59000 of 110034 (53.62%)
Info: Decoding Data24 Section 59500 of 110034 (54.07%)
Info: Decoding Data24 Section 60000 of 110034 (54.53%)
Info: Decoding Data24 Section 60500 of 110034 (54.98%)
Info: Decoding Data24 Section 61000 of 110034 (55.44%)
Info: Decoding Data24 Section 61500 of 110034 (55.89%)
Info: Decoding Data24 Section 62000 of 110034 (56.35%)
Info: Decoding Data24 Section 62500 of 110034 (56.80%)
Info: Decoding Data24 Section 63000 of 110034 (57.26%)
Info: Decoding Data24 Section 63500 of 110034 (57.71%)
Info: Decoding Data24 Section 64000 of 110034 (58.16%)
Info: Decoding Data24 Section 64500 of 110034 (58.62%)
Info: Decoding Data24 Section 65000 of 110034 (59.07%)
Info: Decoding Data24 Section 65500 of 110034 (59.53%)
Info: Decoding Data24 Section 66000 of 110034 (59.98%)
Info: Decoding Data24 Section 66500 of 110034 (60.44%)
Info: Decoding Data24 Section 67000 of 110034 (60.89%)
Info: Decoding Data24 Section 67500 of 110034 (61.34%)
Info: Decoding Data24 Section 68000 of 110034 (61.80%)
Info: Decoding Data24 Section 68500 of 110034 (62.25%)
Info: Decoding Data24 Section 69000 of 110034 (62.71%)
Info: Decoding Data24 Section 69500 of 110034 (63.16%)
Info: Decoding Data24 Section 70000 of 110034 (63.62%)
Info: Decoding Data24 Section 70500 of 110034 (64.07%)
Info: Decoding Data24 Section 71000 of 110034 (64.53%)
Info: Decoding Data24 Section 71500 of 110034 (64.98%)
Info: Decoding Data24 Section 72000 of 110034 (65.43%)
Info: Decoding Data24 Section 72500 of 110034 (65.89%)
Info: Decoding Data24 Section 73000 of 110034 (66.34%)
Info: Decoding Data24 Section 73500 of 110034 (66.80%)
Info: Decoding Data24 Section 74000 of 110034 (67.25%)
Info: Decoding Data24 Section 74500 of 110034 (67.71%)
Info: Decoding Data24 Section 75000 of 110034 (68.16%)
Info: Decoding Data24 Section 75500 of 110034 (68.62%)
Info: Decoding Data24 Section 76000 of 110034 (69.07%)
Info: Decoding Data24 Section 76500 of 110034 (69.52%)
Info: Decoding Data24 Section 77000 of 110034 (69.98%)
Info: Decoding Data24 Section 77500 of 110034 (70.43%)
Info: Decoding Data24 Section 78000 of 110034 (70.89%)
Info: Decoding Data24 Section 78500 of 110034 (71.34%)
Info: Decoding Data24 Section 79000 of 110034 (71.80%)
Info: Decoding Data24 Section 79500 of 110034 (72.25%)
Info: Decoding Data24 Section 80000 of 110034 (72.70%)
Info: Decoding Data24 Section 80500 of 110034 (73.16%)
Info: Decoding Data24 Section 81000 of 110034 (73.61%)
Info: Decoding Data24 Section 81500 of 110034 (74.07%)
Info: Decoding Data24 Section 82000 of 110034 (74.52%)
Info: Decoding Data24 Section 82500 of 110034 (74.98%)
Info: Decoding Data24 Section 83000 of 110034 (75.43%)
Info: Decoding Data24 Section 83500 of 110034 (75.89%)
Info: Decoding Data24 Section 84000 of 110034 (76.34%)
Info: Decoding Data24 Section 84500 of 110034 (76.79%)
Info: Decoding Data24 Section 85000 of 110034 (77.25%)
Info: Decoding Data24 Section 85500 of 110034 (77.70%)
Info: Decoding Data24 Section 86000 of 110034 (78.16%)
Info: Decoding Data24 Section 86500 of 110034 (78.61%)
Info: Decoding Data24 Section 87000 of 110034 (79.07%)
Info: Decoding Data24 Section 87500 of 110034 (79.52%)
Info: Decoding Data24 Section 88000 of 110034 (79.98%)
Info: Decoding Data24 Section 88500 of 110034 (80.43%)
Info: Decoding Data24 Section 89000 of 110034 (80.88%)
Info: Decoding Data24 Section 89500 of 110034 (81.34%)
Info: Decoding Data24 Section 90000 of 110034 (81.79%)
Info: Decoding Data24 Section 90500 of 110034 (82.25%)
Info: Decoding Data24 Section 91000 of 110034 (82.70%)
Info: Decoding Data24 Section 91500 of 110034 (83.16%)
Info: Decoding Data24 Section 92000 of 110034 (83.61%)
Info: Decoding Data24 Section 92500 of 110034 (84.06%)
Info: Decoding Data24 Section 93000 of 110034 (84.52%)
Info: Decoding Data24 Section 93500 of 110034 (84.97%)
Info: Decoding Data24 Section 94000 of 110034 (85.43%)
Info: Decoding Data24 Section 94500 of 110034 (85.88%)
Info: Decoding Data24 Section 95000 of 110034 (86.34%)
Info: Decoding Data24 Section 95500 of 110034 (86.79%)
Info: Decoding Data24 Section 96000 of 110034 (87.25%)
Info: Decoding Data24 Section 96500 of 110034 (87.70%)
Info: Decoding Data24 Section 97000 of 110034 (88.15%)
Info: Decoding Data24 Section 97500 of 110034 (88.61%)
Info: Decoding Data24 Section 98000 of 110034 (89.06%)
Info: Decoding Data24 Section 98500 of 110034 (89.52%)
Info: Decoding Data24 Section 99000 of 110034 (89.97%)
Info: Decoding Data24 Section 99500 of 110034 (90.43%)
Info: Decoding Data24 Section 100000 of 110034 (90.88%)
Info: Decoding Data24 Section 100500 of 110034 (91.34%)
Info: Decoding Data24 Section 101000 of 110034 (91.79%)
Info: Decoding Data24 Section 101500 of 110034 (92.24%)
Info: Decoding Data24 Section 102000 of 110034 (92.70%)
Info: Decoding Data24 Section 102500 of 110034 (93.15%)
Info: Decoding Data24 Section 103000 of 110034 (93.61%)
Info: Decoding Data24 Section 103500 of 110034 (94.06%)
Info: Decoding Data24 Section 104000 of 110034 (94.52%)
Info: Decoding Data24 Section 104500 of 110034 (94.97%)
Info: Decoding Data24 Section 105000 of 110034 (95.43%)
Info: Decoding Data24 Section 105500 of 110034 (95.88%)
Info: Decoding Data24 Section 106000 of 110034 (96.33%)
Info: Decoding Data24 Section 106500 of 110034 (96.79%)
Info: Decoding Data24 Section 107000 of 110034 (97.24%)
Info: Decoding Data24 Section 107500 of 110034 (97.70%)
Info: Decoding Data24 Section 108000 of 110034 (98.15%)
Info: Decoding Data24 Section 108500 of 110034 (98.61%)
Info: Decoding Data24 Section 109000 of 110034 (99.06%)
Info: Decoding Data24 Section 109500 of 110034 (99.51%)
Info: Decoding Data24 Section 110000 of 110034 (99.97%)
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: Data24 to Audio statistics:
Info:   Data24 Frames:
Info:     Total Frames: 10783332
Info:     Valid Frames: 10783332
Info:     Invalid Frames: 0
Info:     Invalid Bytes: 0
Info:   Audio Samples:
Info:     Total samples: 129399984
Info:     Valid samples: 129399984
Info:     Invalid samples: 0
Info:   Section time information:
Info:     Start time: 00:00:00
Info:     End time: 24:27:08
Info:     Total time: 24:27:08
Info: 
Info: Audio correction statistics:
Info:   Total mono samples: 0
Info:   Valid mono samples: 0
Info:   Concealed mono samples: 0
Info:   Silenced mono samples: 0
Info: 
Info: Decoder processing summary (audio):
Info:   Data24 to Audio processing time: 3127 ms
Info:   Audio correction processing time: 711 ms
Info:   Total processing time: 3839 ms (3.84 seconds)
Info: Encoding complete

real	0m15.780s
user	0m15.044s
sys	0m0.358s

### WAV SHA256

[sdi@titan:~/raid/decodes/efm_tests/audio]$ sha256sum roger_rabbit.wav
069a092453be6058e5a047984407ca37c3204e9f7a89a386135c59283fc92fe6  roger_rabbit.wav

# Data
## Input file
DS2_comS1.efm

### EFM SHA256

[sdi@titan:~/raid/decodes/efm_tests/data]$ sha256sum DS2_comS1.efm 
a841ef2350b7ff2923ab1184bdc711b50fb65ae8552d9ac6d6ecf7ab495759ce  DS2_comS1.efm

## EFM to F2
[sdi@titan:~/raid/decodes/efm_tests/data]$ time efm-decoder-f2 DS2_comS1.efm DS2_comS1.f2
Info: Beginning EFM decoding of "DS2_comS1.efm"
Warning: F2SectionCorrection::waitingForSection(): Missing section gap of 5583 is larger than 5, expected absolute time is "00:03:58" actual absolute time is "01:18:16"
Warning: F2SectionCorrection::waitingForSection(): Gaps greater than 5 frames will be treated as padding sections (i.e. the decoder thinks there is a gap in the EFM data rather than actual data loss).
Warning: F2SectionCorrection::waitingForSection(): 5583 missing sections detected, expected absolute time is "00:03:58" actual absolute time is "01:18:16"
Info: Progress: 5 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "02:28:73" actual absolute time is "02:28:74"
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 10 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 15 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 20 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 25 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 30 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:10:18" actual absolute time is "12:10:19"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:10:21" actual absolute time is "12:10:22"
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:18" actual absolute time is "12:12:19"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:24" actual absolute time is "12:12:25"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:27" actual absolute time is "12:12:28"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:30" actual absolute time is "12:12:31"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:39" actual absolute time is "12:12:40"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:42" actual absolute time is "12:12:43"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:51" actual absolute time is "12:12:52"
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "12:12:54" actual absolute time is "12:12:55"
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 35 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "15:00:08" actual absolute time is "15:00:09"
Info: Progress: 40 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 45 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 50 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 55 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 60 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 65 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 70 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F2SectionCorrection::waitingForSection(): Missing section detected, expected absolute time is "28:01:51" actual absolute time is "28:01:52"
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 75 %
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Warning: F3FrameToF2Section::outputSection - Repaired section has a large time difference from the last section - marking as invalid
Info: Progress: 80 %
Info: Progress: 85 %
Info: Progress: 90 %
Info: Progress: 95 %
Info: Progress: 100 %
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: T-values to Channel Frame statistics:
Info:   T-Values:
Info:     Consumed: 1554350148
Info:     Discarded: 562127423
Info:   Channel frames:
Info:     Total: 12680325
Info:     588 bits: 12674389
Info:     >588 bits: 3004
Info:     <588 bits: 2932
Info:   Sync headers:
Info:     Good syncs: 12657037
Info:     Overshoots: 11568
Info:     Undershoots: 378
Info:     Guessed: 11342
Info: 
Info: Channel to F3 Frame statistics:
Info:   Channel Frames:
Info:     Total: 12680325
Info:     Good: 12674440
Info:     Undershoot: 2912
Info:     Overshoot: 2973
Info:   EFM symbols:
Info:     Valid: 405764103
Info:     Invalid: 6022
Info:   Subcode symbols:
Info:     Valid: 12680076
Info:     Invalid: 249
Info: 
Info: F3 Frame to F2 Section statistics:
Info:   F3 Frames:
Info:     Input frames: 12680325
Info:     Good sync0 frames: 129241
Info:     Missing sync0 frames: 89
Info:     Undershoot sync0 frames: 340
Info:     Overshoot sync0 frames: 25
Info:     Lost sync: 0
Info:   Frame loss:
Info:     Presync discarded F3 frames: 0
Info:     Discarded F3 frames: 1257
Info:     Padded F3 frames: 136
Info: 
Info: F2 Section Metadata Correction statistics:
Info:   F2 Sections:
Info:     Total: 134974 (13227452 F2)
Info:     Corrected: 809
Info:     Uncorrectable: 0
Info:     Pre-Leadin: 0
Info:     Missing: 13
Info:     Padding: 5583
Info:     Out of order: 0
Info:   QMode Sections:
Info:     QMode 1 (CD Data): 133673
Info:     QMode 2 (Catalogue No.): 1301
Info:     QMode 3 (ISO 3901 ISRC): 0
Info:     QMode 4 (LD Data): 0
Info:   Absolute Time:
Info:     Start time: 00:01:71
Info:     End time: 30:01:44
Info:     Duration: 29:59:48
Info:   Track 1:
Info:     Start time: 00:00:00
Info:     End time: 29:59:44
Info:     Duration: 29:59:44
Info: 
Info: Decoder processing summary (general):
Info:   Channel to F3 processing time: 15096 ms
Info:   F3 to F2 section processing time: 2415 ms
Info:   F2 correction processing time: 1789 ms
Info:   Total processing time: 19301 ms (19.30 seconds)
Info: 

real	0m37.797s
user	0m36.075s
sys	0m0.986s

### F2 SHA256
[sdi@titan:~/raid/decodes/efm_tests/data]$ sha256sum DS2_comS1.f2 
b164ec8bf0f94318c12dd0094f11a202901edeeb2f04d60f6f5a7b97432774ac  DS2_comS1.f2

## F2 to D24
[sdi@titan:~/raid/decodes/efm_tests/data]$ time efm-decoder-d24 DS2_comS1.f2 DS2_comS1.d24
Info: Beginning EFM decoding of "DS2_comS1.f2"
Info: Decoding F2 Section 0 of 134974 (0.00%)
Info: Decoding F2 Section 1000 of 134974 (0.74%)
Info: Decoding F2 Section 2000 of 134974 (1.48%)
Info: Decoding F2 Section 3000 of 134974 (2.22%)
Info: Decoding F2 Section 4000 of 134974 (2.96%)
Info: Decoding F2 Section 5000 of 134974 (3.70%)
Info: Decoding F2 Section 6000 of 134974 (4.45%)
Info: Decoding F2 Section 7000 of 134974 (5.19%)
Info: Decoding F2 Section 8000 of 134974 (5.93%)
Info: Decoding F2 Section 9000 of 134974 (6.67%)
Info: Decoding F2 Section 10000 of 134974 (7.41%)
Info: Decoding F2 Section 11000 of 134974 (8.15%)
Info: Decoding F2 Section 12000 of 134974 (8.89%)
Info: Decoding F2 Section 13000 of 134974 (9.63%)
Info: Decoding F2 Section 14000 of 134974 (10.37%)
Info: Decoding F2 Section 15000 of 134974 (11.11%)
Info: Decoding F2 Section 16000 of 134974 (11.85%)
Info: Decoding F2 Section 17000 of 134974 (12.60%)
Info: Decoding F2 Section 18000 of 134974 (13.34%)
Info: Decoding F2 Section 19000 of 134974 (14.08%)
Info: Decoding F2 Section 20000 of 134974 (14.82%)
Info: Decoding F2 Section 21000 of 134974 (15.56%)
Info: Decoding F2 Section 22000 of 134974 (16.30%)
Info: Decoding F2 Section 23000 of 134974 (17.04%)
Info: Decoding F2 Section 24000 of 134974 (17.78%)
Info: Decoding F2 Section 25000 of 134974 (18.52%)
Info: Decoding F2 Section 26000 of 134974 (19.26%)
Info: Decoding F2 Section 27000 of 134974 (20.00%)
Info: Decoding F2 Section 28000 of 134974 (20.74%)
Info: Decoding F2 Section 29000 of 134974 (21.49%)
Info: Decoding F2 Section 30000 of 134974 (22.23%)
Info: Decoding F2 Section 31000 of 134974 (22.97%)
Info: Decoding F2 Section 32000 of 134974 (23.71%)
Info: Decoding F2 Section 33000 of 134974 (24.45%)
Info: Decoding F2 Section 34000 of 134974 (25.19%)
Info: Decoding F2 Section 35000 of 134974 (25.93%)
Info: Decoding F2 Section 36000 of 134974 (26.67%)
Info: Decoding F2 Section 37000 of 134974 (27.41%)
Info: Decoding F2 Section 38000 of 134974 (28.15%)
Info: Decoding F2 Section 39000 of 134974 (28.89%)
Info: Decoding F2 Section 40000 of 134974 (29.64%)
Info: Decoding F2 Section 41000 of 134974 (30.38%)
Info: Decoding F2 Section 42000 of 134974 (31.12%)
Info: Decoding F2 Section 43000 of 134974 (31.86%)
Info: Decoding F2 Section 44000 of 134974 (32.60%)
Info: Decoding F2 Section 45000 of 134974 (33.34%)
Info: Decoding F2 Section 46000 of 134974 (34.08%)
Info: Decoding F2 Section 47000 of 134974 (34.82%)
Info: Decoding F2 Section 48000 of 134974 (35.56%)
Info: Decoding F2 Section 49000 of 134974 (36.30%)
Info: Decoding F2 Section 50000 of 134974 (37.04%)
Info: Decoding F2 Section 51000 of 134974 (37.79%)
Info: Decoding F2 Section 52000 of 134974 (38.53%)
Info: Decoding F2 Section 53000 of 134974 (39.27%)
Info: Decoding F2 Section 54000 of 134974 (40.01%)
Info: Decoding F2 Section 55000 of 134974 (40.75%)
Info: Decoding F2 Section 56000 of 134974 (41.49%)
Info: Decoding F2 Section 57000 of 134974 (42.23%)
Info: Decoding F2 Section 58000 of 134974 (42.97%)
Info: Decoding F2 Section 59000 of 134974 (43.71%)
Info: Decoding F2 Section 60000 of 134974 (44.45%)
Info: Decoding F2 Section 61000 of 134974 (45.19%)
Info: Decoding F2 Section 62000 of 134974 (45.93%)
Info: Decoding F2 Section 63000 of 134974 (46.68%)
Info: Decoding F2 Section 64000 of 134974 (47.42%)
Info: Decoding F2 Section 65000 of 134974 (48.16%)
Info: Decoding F2 Section 66000 of 134974 (48.90%)
Info: Decoding F2 Section 67000 of 134974 (49.64%)
Info: Decoding F2 Section 68000 of 134974 (50.38%)
Info: Decoding F2 Section 69000 of 134974 (51.12%)
Info: Decoding F2 Section 70000 of 134974 (51.86%)
Info: Decoding F2 Section 71000 of 134974 (52.60%)
Info: Decoding F2 Section 72000 of 134974 (53.34%)
Info: Decoding F2 Section 73000 of 134974 (54.08%)
Info: Decoding F2 Section 74000 of 134974 (54.83%)
Info: Decoding F2 Section 75000 of 134974 (55.57%)
Info: Decoding F2 Section 76000 of 134974 (56.31%)
Info: Decoding F2 Section 77000 of 134974 (57.05%)
Info: Decoding F2 Section 78000 of 134974 (57.79%)
Info: Decoding F2 Section 79000 of 134974 (58.53%)
Info: Decoding F2 Section 80000 of 134974 (59.27%)
Info: Decoding F2 Section 81000 of 134974 (60.01%)
Info: Decoding F2 Section 82000 of 134974 (60.75%)
Info: Decoding F2 Section 83000 of 134974 (61.49%)
Info: Decoding F2 Section 84000 of 134974 (62.23%)
Info: Decoding F2 Section 85000 of 134974 (62.98%)
Info: Decoding F2 Section 86000 of 134974 (63.72%)
Info: Decoding F2 Section 87000 of 134974 (64.46%)
Info: Decoding F2 Section 88000 of 134974 (65.20%)
Info: Decoding F2 Section 89000 of 134974 (65.94%)
Info: Decoding F2 Section 90000 of 134974 (66.68%)
Info: Decoding F2 Section 91000 of 134974 (67.42%)
Info: Decoding F2 Section 92000 of 134974 (68.16%)
Info: Decoding F2 Section 93000 of 134974 (68.90%)
Info: Decoding F2 Section 94000 of 134974 (69.64%)
Info: Decoding F2 Section 95000 of 134974 (70.38%)
Info: Decoding F2 Section 96000 of 134974 (71.12%)
Info: Decoding F2 Section 97000 of 134974 (71.87%)
Info: Decoding F2 Section 98000 of 134974 (72.61%)
Info: Decoding F2 Section 99000 of 134974 (73.35%)
Info: Decoding F2 Section 100000 of 134974 (74.09%)
Info: Decoding F2 Section 101000 of 134974 (74.83%)
Info: Decoding F2 Section 102000 of 134974 (75.57%)
Info: Decoding F2 Section 103000 of 134974 (76.31%)
Info: Decoding F2 Section 104000 of 134974 (77.05%)
Info: Decoding F2 Section 105000 of 134974 (77.79%)
Info: Decoding F2 Section 106000 of 134974 (78.53%)
Info: Decoding F2 Section 107000 of 134974 (79.27%)
Info: Decoding F2 Section 108000 of 134974 (80.02%)
Info: Decoding F2 Section 109000 of 134974 (80.76%)
Info: Decoding F2 Section 110000 of 134974 (81.50%)
Info: Decoding F2 Section 111000 of 134974 (82.24%)
Info: Decoding F2 Section 112000 of 134974 (82.98%)
Info: Decoding F2 Section 113000 of 134974 (83.72%)
Info: Decoding F2 Section 114000 of 134974 (84.46%)
Info: Decoding F2 Section 115000 of 134974 (85.20%)
Info: Decoding F2 Section 116000 of 134974 (85.94%)
Info: Decoding F2 Section 117000 of 134974 (86.68%)
Info: Decoding F2 Section 118000 of 134974 (87.42%)
Info: Decoding F2 Section 119000 of 134974 (88.17%)
Info: Decoding F2 Section 120000 of 134974 (88.91%)
Info: Decoding F2 Section 121000 of 134974 (89.65%)
Info: Decoding F2 Section 122000 of 134974 (90.39%)
Info: Decoding F2 Section 123000 of 134974 (91.13%)
Info: Decoding F2 Section 124000 of 134974 (91.87%)
Info: Decoding F2 Section 125000 of 134974 (92.61%)
Info: Decoding F2 Section 126000 of 134974 (93.35%)
Info: Decoding F2 Section 127000 of 134974 (94.09%)
Info: Decoding F2 Section 128000 of 134974 (94.83%)
Info: Decoding F2 Section 129000 of 134974 (95.57%)
Info: Decoding F2 Section 130000 of 134974 (96.31%)
Info: Decoding F2 Section 131000 of 134974 (97.06%)
Info: Decoding F2 Section 132000 of 134974 (97.80%)
Info: Decoding F2 Section 133000 of 134974 (98.54%)
Info: Decoding F2 Section 134000 of 134974 (99.28%)
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: F2 Section to F1 Section statistics:
Info:   Input F2 Frames:
Info:     Valid frames: 13220155
Info:     Corrupt frames: 7297 frames containing 51392 byte errors
Info:     Delay line lost frames: 111
Info:     Continuity errors: 0
Info:   Output F1 Frames (after CIRC):
Info:     Valid frames: 12643166
Info:     Invalid frames due to padding: 547239
Info:     Invalid frames without padding: 36936
Info:     Invalid frames (total): 584175
Info:     Output byte errors: 873912
Info:   C1 decoder:
Info:     Valid C1s: 12939836
Info:     Fixed C1s: 277031
Info:     Error C1s: 10584
Info:   C2 decoder:
Info:     Valid C2s: 13091474
Info:     Fixed C2s: 99456
Info:     Error C2s: 36413
Info: 
Info: F1 Section to Data24 Section statistics:
Info:   Frames:
Info:     Total F1 frames: 13227452
Info:     Error-free F1 frames: 13190321
Info:     F1 frames containing errors: 37131
Info:     Padded F1 frames: 547239
Info:     Unpadded F1 frames: 12680213
Info:   Data:
Info:     Total MBytes: 303.59
Info:     Valid MBytes: 302.75
Info:     Corrupt MBytes: 0.83
Info:     Padded MBytes: 12.52
Info:     Data loss: 0.275%
Info: 
Info: Decoder processing summary (general):
Info:   F2 to F1 processing time: 36415 ms
Info:   F1 to Data24 processing time: 3724 ms
Info:   Total processing time: 40140 ms (40.14 seconds)
Info: 
Info: Encoding complete

real	0m51.215s
user	0m49.474s
sys	0m0.671s

### D24 SHA256
[sdi@titan:~/raid/decodes/efm_tests/data]$ sha256sum DS2_comS1.d24
37a2336378bf44a04dcfa4946515c77b43de3f7cd5e62b58dc7ae55b707c6037  DS2_comS1.d24

## D24 to BIN (data)
[sdi@titan:~/raid/decodes/efm_tests/data]$ time efm-decoder-data --output-metadata DS2_comS1.d24 DS2_comS1.bin
Info: Beginning Data24 to ECMA-130 Data decoding of "DS2_comS1.d24"
Info: Decoding Data24 Section 0 of 134974 (0.00%)
Info: Decoding Data24 Section 500 of 134974 (0.37%)
Info: Decoding Data24 Section 1000 of 134974 (0.74%)
Info: Decoding Data24 Section 1500 of 134974 (1.11%)
Info: Decoding Data24 Section 2000 of 134974 (1.48%)
Info: Decoding Data24 Section 2500 of 134974 (1.85%)
Info: Decoding Data24 Section 3000 of 134974 (2.22%)
Info: Decoding Data24 Section 3500 of 134974 (2.59%)
Info: Decoding Data24 Section 4000 of 134974 (2.96%)
Info: Decoding Data24 Section 4500 of 134974 (3.33%)
Info: Decoding Data24 Section 5000 of 134974 (3.70%)
Info: Decoding Data24 Section 5500 of 134974 (4.07%)
Info: Decoding Data24 Section 6000 of 134974 (4.45%)
Info: Decoding Data24 Section 6500 of 134974 (4.82%)
Info: Decoding Data24 Section 7000 of 134974 (5.19%)
Info: Decoding Data24 Section 7500 of 134974 (5.56%)
Info: Decoding Data24 Section 8000 of 134974 (5.93%)
Info: Decoding Data24 Section 8500 of 134974 (6.30%)
Info: Decoding Data24 Section 9000 of 134974 (6.67%)
Info: Decoding Data24 Section 9500 of 134974 (7.04%)
Info: Decoding Data24 Section 10000 of 134974 (7.41%)
Info: Decoding Data24 Section 10500 of 134974 (7.78%)
Info: Decoding Data24 Section 11000 of 134974 (8.15%)
Info: Decoding Data24 Section 11500 of 134974 (8.52%)
Info: Decoding Data24 Section 12000 of 134974 (8.89%)
Info: Decoding Data24 Section 12500 of 134974 (9.26%)
Info: Decoding Data24 Section 13000 of 134974 (9.63%)
Info: Decoding Data24 Section 13500 of 134974 (10.00%)
Info: Decoding Data24 Section 14000 of 134974 (10.37%)
Info: Decoding Data24 Section 14500 of 134974 (10.74%)
Info: Decoding Data24 Section 15000 of 134974 (11.11%)
Info: Decoding Data24 Section 15500 of 134974 (11.48%)
Info: Decoding Data24 Section 16000 of 134974 (11.85%)
Info: Decoding Data24 Section 16500 of 134974 (12.22%)
Info: Decoding Data24 Section 17000 of 134974 (12.60%)
Info: Decoding Data24 Section 17500 of 134974 (12.97%)
Info: Decoding Data24 Section 18000 of 134974 (13.34%)
Info: Decoding Data24 Section 18500 of 134974 (13.71%)
Info: Decoding Data24 Section 19000 of 134974 (14.08%)
Info: Decoding Data24 Section 19500 of 134974 (14.45%)
Info: Decoding Data24 Section 20000 of 134974 (14.82%)
Info: Decoding Data24 Section 20500 of 134974 (15.19%)
Info: Decoding Data24 Section 21000 of 134974 (15.56%)
Info: Decoding Data24 Section 21500 of 134974 (15.93%)
Info: Decoding Data24 Section 22000 of 134974 (16.30%)
Info: Decoding Data24 Section 22500 of 134974 (16.67%)
Info: Decoding Data24 Section 23000 of 134974 (17.04%)
Info: Decoding Data24 Section 23500 of 134974 (17.41%)
Info: Decoding Data24 Section 24000 of 134974 (17.78%)
Info: Decoding Data24 Section 24500 of 134974 (18.15%)
Info: Decoding Data24 Section 25000 of 134974 (18.52%)
Info: Decoding Data24 Section 25500 of 134974 (18.89%)
Info: Decoding Data24 Section 26000 of 134974 (19.26%)
Info: Decoding Data24 Section 26500 of 134974 (19.63%)
Info: Decoding Data24 Section 27000 of 134974 (20.00%)
Info: Decoding Data24 Section 27500 of 134974 (20.37%)
Info: Decoding Data24 Section 28000 of 134974 (20.74%)
Info: Decoding Data24 Section 28500 of 134974 (21.12%)
Info: Decoding Data24 Section 29000 of 134974 (21.49%)
Info: Decoding Data24 Section 29500 of 134974 (21.86%)
Info: Decoding Data24 Section 30000 of 134974 (22.23%)
Info: Decoding Data24 Section 30500 of 134974 (22.60%)
Info: Decoding Data24 Section 31000 of 134974 (22.97%)
Info: Decoding Data24 Section 31500 of 134974 (23.34%)
Info: Decoding Data24 Section 32000 of 134974 (23.71%)
Info: Decoding Data24 Section 32500 of 134974 (24.08%)
Info: Decoding Data24 Section 33000 of 134974 (24.45%)
Info: Decoding Data24 Section 33500 of 134974 (24.82%)
Info: Decoding Data24 Section 34000 of 134974 (25.19%)
Info: Decoding Data24 Section 34500 of 134974 (25.56%)
Info: Decoding Data24 Section 35000 of 134974 (25.93%)
Info: Decoding Data24 Section 35500 of 134974 (26.30%)
Info: Decoding Data24 Section 36000 of 134974 (26.67%)
Info: Decoding Data24 Section 36500 of 134974 (27.04%)
Info: Decoding Data24 Section 37000 of 134974 (27.41%)
Info: Decoding Data24 Section 37500 of 134974 (27.78%)
Info: Decoding Data24 Section 38000 of 134974 (28.15%)
Info: Decoding Data24 Section 38500 of 134974 (28.52%)
Info: Decoding Data24 Section 39000 of 134974 (28.89%)
Info: Decoding Data24 Section 39500 of 134974 (29.26%)
Info: Decoding Data24 Section 40000 of 134974 (29.64%)
Info: Decoding Data24 Section 40500 of 134974 (30.01%)
Info: Decoding Data24 Section 41000 of 134974 (30.38%)
Info: Decoding Data24 Section 41500 of 134974 (30.75%)
Info: Decoding Data24 Section 42000 of 134974 (31.12%)
Info: Decoding Data24 Section 42500 of 134974 (31.49%)
Info: Decoding Data24 Section 43000 of 134974 (31.86%)
Info: Decoding Data24 Section 43500 of 134974 (32.23%)
Info: Decoding Data24 Section 44000 of 134974 (32.60%)
Info: Decoding Data24 Section 44500 of 134974 (32.97%)
Info: Decoding Data24 Section 45000 of 134974 (33.34%)
Info: Decoding Data24 Section 45500 of 134974 (33.71%)
Info: Decoding Data24 Section 46000 of 134974 (34.08%)
Info: Decoding Data24 Section 46500 of 134974 (34.45%)
Info: Decoding Data24 Section 47000 of 134974 (34.82%)
Info: Decoding Data24 Section 47500 of 134974 (35.19%)
Info: Decoding Data24 Section 48000 of 134974 (35.56%)
Info: Decoding Data24 Section 48500 of 134974 (35.93%)
Info: Decoding Data24 Section 49000 of 134974 (36.30%)
Info: Decoding Data24 Section 49500 of 134974 (36.67%)
Info: Decoding Data24 Section 50000 of 134974 (37.04%)
Info: Decoding Data24 Section 50500 of 134974 (37.41%)
Info: Decoding Data24 Section 51000 of 134974 (37.79%)
Info: Decoding Data24 Section 51500 of 134974 (38.16%)
Info: Decoding Data24 Section 52000 of 134974 (38.53%)
Info: Decoding Data24 Section 52500 of 134974 (38.90%)
Info: Decoding Data24 Section 53000 of 134974 (39.27%)
Info: Decoding Data24 Section 53500 of 134974 (39.64%)
Info: Decoding Data24 Section 54000 of 134974 (40.01%)
Info: Decoding Data24 Section 54500 of 134974 (40.38%)
Info: Decoding Data24 Section 55000 of 134974 (40.75%)
Info: Decoding Data24 Section 55500 of 134974 (41.12%)
Info: Decoding Data24 Section 56000 of 134974 (41.49%)
Info: Decoding Data24 Section 56500 of 134974 (41.86%)
Info: Decoding Data24 Section 57000 of 134974 (42.23%)
Info: Decoding Data24 Section 57500 of 134974 (42.60%)
Info: Decoding Data24 Section 58000 of 134974 (42.97%)
Info: Decoding Data24 Section 58500 of 134974 (43.34%)
Info: Decoding Data24 Section 59000 of 134974 (43.71%)
Info: Decoding Data24 Section 59500 of 134974 (44.08%)
Info: Decoding Data24 Section 60000 of 134974 (44.45%)
Info: Decoding Data24 Section 60500 of 134974 (44.82%)
Info: Decoding Data24 Section 61000 of 134974 (45.19%)
Info: Decoding Data24 Section 61500 of 134974 (45.56%)
Info: Decoding Data24 Section 62000 of 134974 (45.93%)
Info: Decoding Data24 Section 62500 of 134974 (46.31%)
Info: Decoding Data24 Section 63000 of 134974 (46.68%)
Info: Decoding Data24 Section 63500 of 134974 (47.05%)
Info: Decoding Data24 Section 64000 of 134974 (47.42%)
Info: Decoding Data24 Section 64500 of 134974 (47.79%)
Info: Decoding Data24 Section 65000 of 134974 (48.16%)
Info: Decoding Data24 Section 65500 of 134974 (48.53%)
Info: Decoding Data24 Section 66000 of 134974 (48.90%)
Info: Decoding Data24 Section 66500 of 134974 (49.27%)
Info: Decoding Data24 Section 67000 of 134974 (49.64%)
Info: Decoding Data24 Section 67500 of 134974 (50.01%)
Info: Decoding Data24 Section 68000 of 134974 (50.38%)
Info: Decoding Data24 Section 68500 of 134974 (50.75%)
Info: Decoding Data24 Section 69000 of 134974 (51.12%)
Info: Decoding Data24 Section 69500 of 134974 (51.49%)
Info: Decoding Data24 Section 70000 of 134974 (51.86%)
Info: Decoding Data24 Section 70500 of 134974 (52.23%)
Info: Decoding Data24 Section 71000 of 134974 (52.60%)
Info: Decoding Data24 Section 71500 of 134974 (52.97%)
Info: Decoding Data24 Section 72000 of 134974 (53.34%)
Info: Decoding Data24 Section 72500 of 134974 (53.71%)
Info: Decoding Data24 Section 73000 of 134974 (54.08%)
Info: Decoding Data24 Section 73500 of 134974 (54.45%)
Info: Decoding Data24 Section 74000 of 134974 (54.83%)
Info: Decoding Data24 Section 74500 of 134974 (55.20%)
Info: Decoding Data24 Section 75000 of 134974 (55.57%)
Info: Decoding Data24 Section 75500 of 134974 (55.94%)
Info: Decoding Data24 Section 76000 of 134974 (56.31%)
Info: Decoding Data24 Section 76500 of 134974 (56.68%)
Info: Decoding Data24 Section 77000 of 134974 (57.05%)
Info: Decoding Data24 Section 77500 of 134974 (57.42%)
Info: Decoding Data24 Section 78000 of 134974 (57.79%)
Info: Decoding Data24 Section 78500 of 134974 (58.16%)
Info: Decoding Data24 Section 79000 of 134974 (58.53%)
Info: Decoding Data24 Section 79500 of 134974 (58.90%)
Info: Decoding Data24 Section 80000 of 134974 (59.27%)
Info: Decoding Data24 Section 80500 of 134974 (59.64%)
Info: Decoding Data24 Section 81000 of 134974 (60.01%)
Info: Decoding Data24 Section 81500 of 134974 (60.38%)
Info: Decoding Data24 Section 82000 of 134974 (60.75%)
Info: Decoding Data24 Section 82500 of 134974 (61.12%)
Info: Decoding Data24 Section 83000 of 134974 (61.49%)
Info: Decoding Data24 Section 83500 of 134974 (61.86%)
Info: Decoding Data24 Section 84000 of 134974 (62.23%)
Info: Decoding Data24 Section 84500 of 134974 (62.60%)
Info: Decoding Data24 Section 85000 of 134974 (62.98%)
Info: Decoding Data24 Section 85500 of 134974 (63.35%)
Info: Decoding Data24 Section 86000 of 134974 (63.72%)
Info: Decoding Data24 Section 86500 of 134974 (64.09%)
Info: Decoding Data24 Section 87000 of 134974 (64.46%)
Info: Decoding Data24 Section 87500 of 134974 (64.83%)
Info: Decoding Data24 Section 88000 of 134974 (65.20%)
Info: Decoding Data24 Section 88500 of 134974 (65.57%)
Info: Decoding Data24 Section 89000 of 134974 (65.94%)
Info: Decoding Data24 Section 89500 of 134974 (66.31%)
Info: Decoding Data24 Section 90000 of 134974 (66.68%)
Info: Decoding Data24 Section 90500 of 134974 (67.05%)
Info: Decoding Data24 Section 91000 of 134974 (67.42%)
Info: Decoding Data24 Section 91500 of 134974 (67.79%)
Info: Decoding Data24 Section 92000 of 134974 (68.16%)
Info: Decoding Data24 Section 92500 of 134974 (68.53%)
Info: Decoding Data24 Section 93000 of 134974 (68.90%)
Info: Decoding Data24 Section 93500 of 134974 (69.27%)
Info: Decoding Data24 Section 94000 of 134974 (69.64%)
Info: Decoding Data24 Section 94500 of 134974 (70.01%)
Info: Decoding Data24 Section 95000 of 134974 (70.38%)
Info: Decoding Data24 Section 95500 of 134974 (70.75%)
Info: Decoding Data24 Section 96000 of 134974 (71.12%)
Info: Decoding Data24 Section 96500 of 134974 (71.50%)
Info: Decoding Data24 Section 97000 of 134974 (71.87%)
Info: Decoding Data24 Section 97500 of 134974 (72.24%)
Info: Decoding Data24 Section 98000 of 134974 (72.61%)
Info: Decoding Data24 Section 98500 of 134974 (72.98%)
Info: Decoding Data24 Section 99000 of 134974 (73.35%)
Info: Decoding Data24 Section 99500 of 134974 (73.72%)
Info: Decoding Data24 Section 100000 of 134974 (74.09%)
Info: Decoding Data24 Section 100500 of 134974 (74.46%)
Info: Decoding Data24 Section 101000 of 134974 (74.83%)
Info: Decoding Data24 Section 101500 of 134974 (75.20%)
Info: Decoding Data24 Section 102000 of 134974 (75.57%)
Info: Decoding Data24 Section 102500 of 134974 (75.94%)
Info: Decoding Data24 Section 103000 of 134974 (76.31%)
Info: Decoding Data24 Section 103500 of 134974 (76.68%)
Info: Decoding Data24 Section 104000 of 134974 (77.05%)
Info: Decoding Data24 Section 104500 of 134974 (77.42%)
Info: Decoding Data24 Section 105000 of 134974 (77.79%)
Info: Decoding Data24 Section 105500 of 134974 (78.16%)
Info: Decoding Data24 Section 106000 of 134974 (78.53%)
Info: Decoding Data24 Section 106500 of 134974 (78.90%)
Info: Decoding Data24 Section 107000 of 134974 (79.27%)
Info: Decoding Data24 Section 107500 of 134974 (79.64%)
Info: Decoding Data24 Section 108000 of 134974 (80.02%)
Info: Decoding Data24 Section 108500 of 134974 (80.39%)
Info: Decoding Data24 Section 109000 of 134974 (80.76%)
Info: Decoding Data24 Section 109500 of 134974 (81.13%)
Info: Decoding Data24 Section 110000 of 134974 (81.50%)
Info: Decoding Data24 Section 110500 of 134974 (81.87%)
Info: Decoding Data24 Section 111000 of 134974 (82.24%)
Info: Decoding Data24 Section 111500 of 134974 (82.61%)
Info: Decoding Data24 Section 112000 of 134974 (82.98%)
Info: Decoding Data24 Section 112500 of 134974 (83.35%)
Info: Decoding Data24 Section 113000 of 134974 (83.72%)
Info: Decoding Data24 Section 113500 of 134974 (84.09%)
Info: Decoding Data24 Section 114000 of 134974 (84.46%)
Info: Decoding Data24 Section 114500 of 134974 (84.83%)
Info: Decoding Data24 Section 115000 of 134974 (85.20%)
Info: Decoding Data24 Section 115500 of 134974 (85.57%)
Info: Decoding Data24 Section 116000 of 134974 (85.94%)
Info: Decoding Data24 Section 116500 of 134974 (86.31%)
Info: Decoding Data24 Section 117000 of 134974 (86.68%)
Info: Decoding Data24 Section 117500 of 134974 (87.05%)
Info: Decoding Data24 Section 118000 of 134974 (87.42%)
Info: Decoding Data24 Section 118500 of 134974 (87.79%)
Info: Decoding Data24 Section 119000 of 134974 (88.17%)
Info: Decoding Data24 Section 119500 of 134974 (88.54%)
Info: Decoding Data24 Section 120000 of 134974 (88.91%)
Info: Decoding Data24 Section 120500 of 134974 (89.28%)
Info: Decoding Data24 Section 121000 of 134974 (89.65%)
Info: Decoding Data24 Section 121500 of 134974 (90.02%)
Info: Decoding Data24 Section 122000 of 134974 (90.39%)
Info: Decoding Data24 Section 122500 of 134974 (90.76%)
Info: Decoding Data24 Section 123000 of 134974 (91.13%)
Info: Decoding Data24 Section 123500 of 134974 (91.50%)
Info: Decoding Data24 Section 124000 of 134974 (91.87%)
Info: Decoding Data24 Section 124500 of 134974 (92.24%)
Info: Decoding Data24 Section 125000 of 134974 (92.61%)
Info: Decoding Data24 Section 125500 of 134974 (92.98%)
Info: Decoding Data24 Section 126000 of 134974 (93.35%)
Info: Decoding Data24 Section 126500 of 134974 (93.72%)
Info: Decoding Data24 Section 127000 of 134974 (94.09%)
Info: Decoding Data24 Section 127500 of 134974 (94.46%)
Info: Decoding Data24 Section 128000 of 134974 (94.83%)
Info: Decoding Data24 Section 128500 of 134974 (95.20%)
Info: Decoding Data24 Section 129000 of 134974 (95.57%)
Info: Decoding Data24 Section 129500 of 134974 (95.94%)
Info: Decoding Data24 Section 130000 of 134974 (96.31%)
Info: Decoding Data24 Section 130500 of 134974 (96.69%)
Info: Decoding Data24 Section 131000 of 134974 (97.06%)
Info: Decoding Data24 Section 131500 of 134974 (97.43%)
Info: Decoding Data24 Section 132000 of 134974 (97.80%)
Info: Decoding Data24 Section 132500 of 134974 (98.17%)
Info: Decoding Data24 Section 133000 of 134974 (98.54%)
Info: Decoding Data24 Section 133500 of 134974 (98.91%)
Info: Decoding Data24 Section 134000 of 134974 (99.28%)
Info: Decoding Data24 Section 134500 of 134974 (99.65%)
Info: Flushing decoding pipelines
Info: Processing final pipeline data
Info: Decoding complete
Info: Data24ToRawSector statistics:
Info:   Valid sectors: 129392
Info:   Discarded bytes: 13126944
Info:   Discarded padding bytes: 0
Info:   Good sync patterns: 129046
Info:   Bad sync patterns: 347
Info:   Missed sync patterns: 0
Info:   Sync lost count: 1
Info: 
Info: Raw Sector to Sector (RSPC error-correction):
Info:   Valid sectors: 128943 (corrected: 16)
Info:   Invalid sectors: 449
Info:   Sector metadata:
Info:     Mode 0 sectors: 0
Info:     Mode 1 sectors: 128930
Info:     Mode 2 sectors: 0
Info:     Invalid mode sectors: 446
Info: 
Info: Sector gap correction:
Info:   Good sectors: 128943
Info:   Missing leading sectors: 144
Info:   Missing/Gap sectors: 6029
Info:   Total sectors: 135116
Info: 
Info: Decoder processing summary (data):
Info:   Data24 to Raw Sector processing time: 14466 ms
Info:   Raw Sector to Sector processing time: 892 ms
Info:   Total processing time: 15359 ms (15.36 seconds)
Info: 
Info: Encoding complete

real	0m15.955s
user	0m15.270s
sys	0m0.187s

### BIN SHA256
[sdi@titan:~/raid/decodes/efm_tests/data]$ sha256sum DS2_comS1.bin
f3a09dd8eb886afdcfaa3ffb56e85c2817137e1d689da250555e37410945997b  DS2_comS1.bin

[sdi@titan:~/raid/decodes/efm_tests/data]$ sha256sum DS2_comS1.bin.bsm 
ec0802421acd415b403ecdce59ab332356c8b328f234a4f8dd597e0a5d0db5e7  DS2_comS1.bin.bsm

# VFS Verifier output
[sdi@titan:~/raid/decodes/efm_tests/data]$ vfs-verifier DS2_comS1.bin DS2_comS1.bin.bsm
Info: Beginning VFS image verification of "DS2_comS1.bin" using bad sector map metadata from "DS2_comS1.bin.bsm"
Critical: AdfsImage::readSectors() - Checksum failed for sector 0 checksum 65526 expected 244
Critical: AdfsImage::readSectors() - Checksum failed for sector 1 checksum 25 expected 22
Info: Directory entries:
Info:   !BOOT LR (01) 0x00000E00 0x00000E00 0x00004050 0x000018
Info:   AREA LR (02) 0x00000000 0x00000000 0x000016FE 0x095160
Info:   CHART LR (03) 0x00000000 0x00000000 0x00004782 0x095280
Info:   CNMAUTO LR (04) 0x00000000 0x00000000 0x000016A0 0x0951A8
Info:   CNMCORR LR (05) 0x00000000 0x00000000 0x0000154A 0x0951C0
Info:   CNMDETL LR (06) 0x00000000 0x00000000 0x00000544 0x0951D8
Info:   CNMDISP LR (07) 0x00000000 0x00000000 0x00000A84 0x0951F0
Info:   CNMLINK LR (08) 0x00000000 0x00000000 0x0000047A 0x095208
Info:   CNMMANU LR (09) 0x00000000 0x00000000 0x00000CDC 0x095220
Info:   CNMRETR LR (10) 0x00000000 0x00000000 0x000017A2 0x095238
Info:   CNMWIND LR (11) 0x00000000 0x00000000 0x0000055E 0x095250
Info:   CNMWRIT LR (12) 0x00000000 0x00000000 0x00000CE0 0x095268
Info:   CONTENTS LR (13) 0x00000000 0x00000000 0x00000D0A 0x092328
Info:   DATA1 LR (14) 0x00000000 0x00000000 0x06414000 0x00E940
Info:   DATA2 LR (15) 0x00000000 0x00000000 0x04C86000 0x0A7820
Info:   FILM LR (16) 0x00000000 0x00000000 0x00000976 0x095310
Info:   FIND LR (17) 0x00000000 0x00000000 0x00002BF0 0x07BFD8
Info:   FONT2 LR (18) 0xFFFF2800 0xFFFF2800 0x00000744 0x000090
Info:   GAZETTEER LR (19) 0x00000000 0x00000000 0x00809800 0x07C908
Info:   HELP LR (20) 0x00000000 0x00000000 0x0000456E 0x07C050
Info:   HELPTEXT LR (21) 0x00000000 0x00000000 0x00042800 0x07C098
Info:   INDEX LR (22) 0x00000000 0x00000000 0x004B3000 0x073380
Info:   INIT LR (23) 0x00000000 0x00000000 0x000001EC 0x000078
Info:   MAP LR (24) 0x00000000 0x00000000 0x00003F70 0x072C90
Info:   MAPDATA1 LR (25) 0x00000000 0x00000000 0x0006A800 0x072CD8
Info:   MAPPROC LR (26) 0x00000000 0x00000000 0x000022F0 0x095178
Info:   NAMES LR (27) 0x00000000 0x00000000 0x00411D8C 0x077EB0
Info:   NATFIND LR (28) 0x00000000 0x00000000 0x00001E92 0x092340
Info:   PHOTO LR (29) 0x00000000 0x00000000 0x00000C36 0x0952C8
Info:   PHTX LR (30) 0x00000000 0x00000000 0x00003310 0x07C008
Info:   RMLHELPT LR (31) 0x00000000 0x00000000 0x00042800 0x07C4D0
Info:   ROOT LR (32) 0x00000000 0x00000000 0x0000048C 0x000060
Info:   STINIT1 LR (33) 0x00000000 0x00000000 0x000015C0 0x0000A8
Info:   STINIT2 LR (34) 0x00000000 0x00000000 0x00000CF8 0x0000C0
Info:   TEXT LR (35) 0x00000000 0x00000000 0x00002210 0x0952E0
Info:   USERKERN LR (36) 0x00000E00 0x00000E00 0x00004096 0x095610
Info:   WALK LR (37) 0x00000000 0x00000000 0x00000AAA 0x092310
Info:   WORDS LR (38) 0x00000000 0x00000000 0x00000290 0x0000D8
Warning: AdfsVerifier::process() - Bad EFM sector 8104 found in file DATA1 ADFS sector 0x00F7A0
Warning: AdfsVerifier::process() - Bad EFM sector 8105 found in file DATA1 ADFS sector 0x00F7A8
Warning: AdfsVerifier::process() - Bad EFM sector 8738 found in file DATA1 ADFS sector 0x010B70
Warning: AdfsVerifier::process() - Bad EFM sector 8739 found in file DATA1 ADFS sector 0x010B78
Warning: AdfsVerifier::process() - Bad EFM sector 8842 found in file DATA1 ADFS sector 0x010EB0
Warning: AdfsVerifier::process() - Bad EFM sector 8843 found in file DATA1 ADFS sector 0x010EB8
Warning: AdfsVerifier::process() - Bad EFM sector 9070 found in file DATA1 ADFS sector 0x0115D0
Warning: AdfsVerifier::process() - Bad EFM sector 9071 found in file DATA1 ADFS sector 0x0115D8
Warning: AdfsVerifier::process() - Bad EFM sector 9804 found in file DATA1 ADFS sector 0x012CC0
Warning: AdfsVerifier::process() - Bad EFM sector 9805 found in file DATA1 ADFS sector 0x012CC8
Warning: AdfsVerifier::process() - Bad EFM sector 10183 found in file DATA1 ADFS sector 0x013898
Warning: AdfsVerifier::process() - Bad EFM sector 10184 found in file DATA1 ADFS sector 0x0138A0
Warning: AdfsVerifier::process() - Bad EFM sector 11089 found in file DATA1 ADFS sector 0x0154E8
Warning: AdfsVerifier::process() - Bad EFM sector 11090 found in file DATA1 ADFS sector 0x0154F0
Warning: AdfsVerifier::process() - Bad EFM sector 11170 found in file DATA1 ADFS sector 0x015770
Warning: AdfsVerifier::process() - Bad EFM sector 11171 found in file DATA1 ADFS sector 0x015778
Warning: AdfsVerifier::process() - Bad EFM sector 12119 found in file DATA1 ADFS sector 0x017518
Warning: AdfsVerifier::process() - Bad EFM sector 12120 found in file DATA1 ADFS sector 0x017520
Warning: AdfsVerifier::process() - Bad EFM sector 12528 found in file DATA1 ADFS sector 0x0181E0
Warning: AdfsVerifier::process() - Bad EFM sector 12529 found in file DATA1 ADFS sector 0x0181E8
Warning: AdfsVerifier::process() - Bad EFM sector 12531 found in file DATA1 ADFS sector 0x0181F8
Warning: AdfsVerifier::process() - Bad EFM sector 12532 found in file DATA1 ADFS sector 0x018200
Warning: AdfsVerifier::process() - Bad EFM sector 14119 found in file DATA1 ADFS sector 0x01B398
Warning: AdfsVerifier::process() - Bad EFM sector 14120 found in file DATA1 ADFS sector 0x01B3A0
Warning: AdfsVerifier::process() - Bad EFM sector 14239 found in file DATA1 ADFS sector 0x01B758
Warning: AdfsVerifier::process() - Bad EFM sector 14240 found in file DATA1 ADFS sector 0x01B760
Warning: AdfsVerifier::process() - Bad EFM sector 14249 found in file DATA1 ADFS sector 0x01B7A8
Warning: AdfsVerifier::process() - Bad EFM sector 14250 found in file DATA1 ADFS sector 0x01B7B0
Warning: AdfsVerifier::process() - Bad EFM sector 14521 found in file DATA1 ADFS sector 0x01C028
Warning: AdfsVerifier::process() - Bad EFM sector 14522 found in file DATA1 ADFS sector 0x01C030
Warning: AdfsVerifier::process() - Bad EFM sector 14896 found in file DATA1 ADFS sector 0x01CBE0
Warning: AdfsVerifier::process() - Bad EFM sector 14897 found in file DATA1 ADFS sector 0x01CBE8
Warning: AdfsVerifier::process() - Bad EFM sector 14950 found in file DATA1 ADFS sector 0x01CD90
Warning: AdfsVerifier::process() - Bad EFM sector 15141 found in file DATA1 ADFS sector 0x01D388
Warning: AdfsVerifier::process() - Bad EFM sector 15142 found in file DATA1 ADFS sector 0x01D390
Warning: AdfsVerifier::process() - Bad EFM sector 16063 found in file DATA1 ADFS sector 0x01F058
Warning: AdfsVerifier::process() - Bad EFM sector 16915 found in file DATA1 ADFS sector 0x020AF8
Warning: AdfsVerifier::process() - Bad EFM sector 17065 found in file DATA1 ADFS sector 0x020FA8
Warning: AdfsVerifier::process() - Bad EFM sector 17066 found in file DATA1 ADFS sector 0x020FB0
Warning: AdfsVerifier::process() - Bad EFM sector 17068 found in file DATA1 ADFS sector 0x020FC0
Warning: AdfsVerifier::process() - Bad EFM sector 17069 found in file DATA1 ADFS sector 0x020FC8
Warning: AdfsVerifier::process() - Bad EFM sector 17332 found in file DATA1 ADFS sector 0x021800
Warning: AdfsVerifier::process() - Bad EFM sector 17333 found in file DATA1 ADFS sector 0x021808
Warning: AdfsVerifier::process() - Bad EFM sector 17629 found in file DATA1 ADFS sector 0x022148
Warning: AdfsVerifier::process() - Bad EFM sector 17630 found in file DATA1 ADFS sector 0x022150
Warning: AdfsVerifier::process() - Bad EFM sector 18247 found in file DATA1 ADFS sector 0x023498
Warning: AdfsVerifier::process() - Bad EFM sector 18248 found in file DATA1 ADFS sector 0x0234A0
Warning: AdfsVerifier::process() - Bad EFM sector 18377 found in file DATA1 ADFS sector 0x0238A8
Warning: AdfsVerifier::process() - Bad EFM sector 18378 found in file DATA1 ADFS sector 0x0238B0
Warning: AdfsVerifier::process() - Bad EFM sector 18682 found in file DATA1 ADFS sector 0x024230
Warning: AdfsVerifier::process() - Bad EFM sector 18683 found in file DATA1 ADFS sector 0x024238
Warning: AdfsVerifier::process() - Bad EFM sector 19642 found in file DATA1 ADFS sector 0x026030
Warning: AdfsVerifier::process() - Bad EFM sector 19643 found in file DATA1 ADFS sector 0x026038
Warning: AdfsVerifier::process() - Bad EFM sector 19672 found in file DATA1 ADFS sector 0x026120
Warning: AdfsVerifier::process() - Bad EFM sector 19673 found in file DATA1 ADFS sector 0x026128
Warning: AdfsVerifier::process() - Bad EFM sector 20484 found in file DATA1 ADFS sector 0x027A80
Warning: AdfsVerifier::process() - Bad EFM sector 20485 found in file DATA1 ADFS sector 0x027A88
Warning: AdfsVerifier::process() - Bad EFM sector 20700 found in file DATA1 ADFS sector 0x028140
Warning: AdfsVerifier::process() - Bad EFM sector 20701 found in file DATA1 ADFS sector 0x028148
Warning: AdfsVerifier::process() - Bad EFM sector 20719 found in file DATA1 ADFS sector 0x0281D8
Warning: AdfsVerifier::process() - Bad EFM sector 20720 found in file DATA1 ADFS sector 0x0281E0
Warning: AdfsVerifier::process() - Bad EFM sector 21034 found in file DATA1 ADFS sector 0x028BB0
Warning: AdfsVerifier::process() - Bad EFM sector 21035 found in file DATA1 ADFS sector 0x028BB8
Warning: AdfsVerifier::process() - Bad EFM sector 21137 found in file DATA1 ADFS sector 0x028EE8
Warning: AdfsVerifier::process() - Bad EFM sector 21235 found in file DATA1 ADFS sector 0x0291F8
Warning: AdfsVerifier::process() - Bad EFM sector 21236 found in file DATA1 ADFS sector 0x029200
Warning: AdfsVerifier::process() - Bad EFM sector 21465 found in file DATA1 ADFS sector 0x029928
Warning: AdfsVerifier::process() - Bad EFM sector 21466 found in file DATA1 ADFS sector 0x029930
Warning: AdfsVerifier::process() - Bad EFM sector 21625 found in file DATA1 ADFS sector 0x029E28
Warning: AdfsVerifier::process() - Bad EFM sector 21626 found in file DATA1 ADFS sector 0x029E30
Warning: AdfsVerifier::process() - Bad EFM sector 22266 found in file DATA1 ADFS sector 0x02B230
Warning: AdfsVerifier::process() - Bad EFM sector 22267 found in file DATA1 ADFS sector 0x02B238
Warning: AdfsVerifier::process() - Bad EFM sector 24481 found in file DATA1 ADFS sector 0x02F768
Warning: AdfsVerifier::process() - Bad EFM sector 24482 found in file DATA1 ADFS sector 0x02F770
Warning: AdfsVerifier::process() - Bad EFM sector 25122 found in file DATA1 ADFS sector 0x030B70
Warning: AdfsVerifier::process() - Bad EFM sector 25123 found in file DATA1 ADFS sector 0x030B78
Warning: AdfsVerifier::process() - Bad EFM sector 25968 found in file DATA1 ADFS sector 0x0325E0
Warning: AdfsVerifier::process() - Bad EFM sector 25969 found in file DATA1 ADFS sector 0x0325E8
Warning: AdfsVerifier::process() - Bad EFM sector 25986 found in file DATA1 ADFS sector 0x032670
Warning: AdfsVerifier::process() - Bad EFM sector 25987 found in file DATA1 ADFS sector 0x032678
Warning: AdfsVerifier::process() - Bad EFM sector 25992 found in file DATA1 ADFS sector 0x0326A0
Warning: AdfsVerifier::process() - Bad EFM sector 25993 found in file DATA1 ADFS sector 0x0326A8
Warning: AdfsVerifier::process() - Bad EFM sector 26028 found in file DATA1 ADFS sector 0x0327C0
Warning: AdfsVerifier::process() - Bad EFM sector 26029 found in file DATA1 ADFS sector 0x0327C8
Warning: AdfsVerifier::process() - Bad EFM sector 27777 found in file DATA1 ADFS sector 0x035E68
Warning: AdfsVerifier::process() - Bad EFM sector 27778 found in file DATA1 ADFS sector 0x035E70
Warning: AdfsVerifier::process() - Bad EFM sector 27780 found in file DATA1 ADFS sector 0x035E80
Warning: AdfsVerifier::process() - Bad EFM sector 27781 found in file DATA1 ADFS sector 0x035E88
Warning: AdfsVerifier::process() - Bad EFM sector 28774 found in file DATA1 ADFS sector 0x037D90
Warning: AdfsVerifier::process() - Bad EFM sector 29759 found in file DATA1 ADFS sector 0x039C58
Warning: AdfsVerifier::process() - Bad EFM sector 29760 found in file DATA1 ADFS sector 0x039C60
Warning: AdfsVerifier::process() - Bad EFM sector 29862 found in file DATA1 ADFS sector 0x039F90
Warning: AdfsVerifier::process() - Bad EFM sector 29863 found in file DATA1 ADFS sector 0x039F98
Warning: AdfsVerifier::process() - Bad EFM sector 32830 found in file DATA1 ADFS sector 0x03FC50
Warning: AdfsVerifier::process() - Bad EFM sector 32831 found in file DATA1 ADFS sector 0x03FC58
Warning: AdfsVerifier::process() - Bad EFM sector 33139 found in file DATA1 ADFS sector 0x0405F8
Warning: AdfsVerifier::process() - Bad EFM sector 33140 found in file DATA1 ADFS sector 0x040600
Warning: AdfsVerifier::process() - Bad EFM sector 33950 found in file DATA1 ADFS sector 0x041F50
Warning: AdfsVerifier::process() - Bad EFM sector 33951 found in file DATA1 ADFS sector 0x041F58
Warning: AdfsVerifier::process() - Bad EFM sector 34009 found in file DATA1 ADFS sector 0x042128
Warning: AdfsVerifier::process() - Bad EFM sector 34010 found in file DATA1 ADFS sector 0x042130
Warning: AdfsVerifier::process() - Bad EFM sector 35676 found in file DATA1 ADFS sector 0x045540
Warning: AdfsVerifier::process() - Bad EFM sector 35677 found in file DATA1 ADFS sector 0x045548
Warning: AdfsVerifier::process() - Bad EFM sector 36195 found in file DATA1 ADFS sector 0x046578
Warning: AdfsVerifier::process() - Bad EFM sector 36196 found in file DATA1 ADFS sector 0x046580
Warning: AdfsVerifier::process() - Bad EFM sector 37033 found in file DATA1 ADFS sector 0x047FA8
Warning: AdfsVerifier::process() - Bad EFM sector 37034 found in file DATA1 ADFS sector 0x047FB0
Warning: AdfsVerifier::process() - Bad EFM sector 37039 found in file DATA1 ADFS sector 0x047FD8
Warning: AdfsVerifier::process() - Bad EFM sector 37040 found in file DATA1 ADFS sector 0x047FE0
Warning: AdfsVerifier::process() - Bad EFM sector 39495 found in file DATA1 ADFS sector 0x04CC98
Warning: AdfsVerifier::process() - Bad EFM sector 39496 found in file DATA1 ADFS sector 0x04CCA0
Warning: AdfsVerifier::process() - Bad EFM sector 39817 found in file DATA1 ADFS sector 0x04D6A8
Warning: AdfsVerifier::process() - Bad EFM sector 39818 found in file DATA1 ADFS sector 0x04D6B0
Warning: AdfsVerifier::process() - Bad EFM sector 40596 found in file DATA1 ADFS sector 0x04EF00
Warning: AdfsVerifier::process() - Bad EFM sector 40597 found in file DATA1 ADFS sector 0x04EF08
Warning: AdfsVerifier::process() - Bad EFM sector 41618 found in file DATA1 ADFS sector 0x050EF0
Warning: AdfsVerifier::process() - Bad EFM sector 41619 found in file DATA1 ADFS sector 0x050EF8
Warning: AdfsVerifier::process() - Bad EFM sector 42700 found in file DATA1 ADFS sector 0x0530C0
Warning: AdfsVerifier::process() - Bad EFM sector 42701 found in file DATA1 ADFS sector 0x0530C8
Warning: AdfsVerifier::process() - Bad EFM sector 43186 found in file DATA1 ADFS sector 0x053FF0
Warning: AdfsVerifier::process() - Bad EFM sector 43187 found in file DATA1 ADFS sector 0x053FF8
Warning: AdfsVerifier::process() - Bad EFM sector 44169 found in file DATA1 ADFS sector 0x055EA8
Warning: AdfsVerifier::process() - Bad EFM sector 44170 found in file DATA1 ADFS sector 0x055EB0
Warning: AdfsVerifier::process() - Bad EFM sector 44299 found in file DATA1 ADFS sector 0x0562B8
Warning: AdfsVerifier::process() - Bad EFM sector 44300 found in file DATA1 ADFS sector 0x0562C0
Warning: AdfsVerifier::process() - Bad EFM sector 45116 found in file DATA1 ADFS sector 0x057C40
Warning: AdfsVerifier::process() - Bad EFM sector 45117 found in file DATA1 ADFS sector 0x057C48
Warning: AdfsVerifier::process() - Bad EFM sector 49677 found in file DATA1 ADFS sector 0x060AC8
Warning: AdfsVerifier::process() - Bad EFM sector 49678 found in file DATA1 ADFS sector 0x060AD0
Warning: AdfsVerifier::process() - Bad EFM sector 49870 found in file DATA1 ADFS sector 0x0610D0
Warning: AdfsVerifier::process() - Bad EFM sector 49871 found in file DATA1 ADFS sector 0x0610D8
Warning: AdfsVerifier::process() - Bad EFM sector 50740 found in file DATA1 ADFS sector 0x062C00
Warning: AdfsVerifier::process() - Bad EFM sector 50741 found in file DATA1 ADFS sector 0x062C08
Warning: AdfsVerifier::process() - Bad EFM sector 51864 found in file DATA1 ADFS sector 0x064F20
Warning: AdfsVerifier::process() - Bad EFM sector 51865 found in file DATA1 ADFS sector 0x064F28
Warning: AdfsVerifier::process() - Bad EFM sector 54765 found in file DATA1 ADFS sector 0x06A9C8
Warning: AdfsVerifier::process() - Bad EFM sector 54766 found in file DATA1 ADFS sector 0x06A9D0
Warning: AdfsVerifier::process() - Bad EFM sector 54768 found in file DATA1 ADFS sector 0x06A9E0
Warning: AdfsVerifier::process() - Bad EFM sector 54769 found in file DATA1 ADFS sector 0x06A9E8
Warning: AdfsVerifier::process() - Bad EFM sector 54915 found in file DATA1 ADFS sector 0x06AE78
Warning: AdfsVerifier::process() - Bad EFM sector 54916 found in file DATA1 ADFS sector 0x06AE80
Warning: AdfsVerifier::process() - Bad EFM sector 54918 found in file DATA1 ADFS sector 0x06AE90
Warning: AdfsVerifier::process() - Bad EFM sector 54919 found in file DATA1 ADFS sector 0x06AE98
Warning: AdfsVerifier::process() - Bad EFM sector 54921 found in file DATA1 ADFS sector 0x06AEA8
Warning: AdfsVerifier::process() - Bad EFM sector 54922 found in file DATA1 ADFS sector 0x06AEB0
Warning: AdfsVerifier::process() - Bad EFM sector 54924 found in file DATA1 ADFS sector 0x06AEC0
Warning: AdfsVerifier::process() - Bad EFM sector 54925 found in file DATA1 ADFS sector 0x06AEC8
Warning: AdfsVerifier::process() - Bad EFM sector 54927 found in file DATA1 ADFS sector 0x06AED8
Warning: AdfsVerifier::process() - Bad EFM sector 54928 found in file DATA1 ADFS sector 0x06AEE0
Warning: AdfsVerifier::process() - Bad EFM sector 54933 found in file DATA1 ADFS sector 0x06AF08
Warning: AdfsVerifier::process() - Bad EFM sector 54934 found in file DATA1 ADFS sector 0x06AF10
Warning: AdfsVerifier::process() - Bad EFM sector 54936 found in file DATA1 ADFS sector 0x06AF20
Warning: AdfsVerifier::process() - Bad EFM sector 54937 found in file DATA1 ADFS sector 0x06AF28
Warning: AdfsVerifier::process() - Bad EFM sector 54939 found in file DATA1 ADFS sector 0x06AF38
Warning: AdfsVerifier::process() - Bad EFM sector 54940 found in file DATA1 ADFS sector 0x06AF40
Warning: AdfsVerifier::process() - Bad EFM sector 54948 found in file DATA1 ADFS sector 0x06AF80
Warning: AdfsVerifier::process() - Bad EFM sector 54949 found in file DATA1 ADFS sector 0x06AF88
Warning: AdfsVerifier::process() - Bad EFM sector 54951 found in file DATA1 ADFS sector 0x06AF98
Warning: AdfsVerifier::process() - Bad EFM sector 54952 found in file DATA1 ADFS sector 0x06AFA0
Warning: AdfsVerifier::process() - Bad EFM sector 54954 found in file DATA1 ADFS sector 0x06AFB0
Warning: AdfsVerifier::process() - Bad EFM sector 54957 found in file DATA1 ADFS sector 0x06AFC8
Warning: AdfsVerifier::process() - Bad EFM sector 54958 found in file DATA1 ADFS sector 0x06AFD0
Warning: AdfsVerifier::process() - Bad EFM sector 54993 found in file DATA1 ADFS sector 0x06B0E8
Warning: AdfsVerifier::process() - Bad EFM sector 54994 found in file DATA1 ADFS sector 0x06B0F0
Warning: AdfsVerifier::process() - Bad EFM sector 54999 found in file DATA1 ADFS sector 0x06B118
Warning: AdfsVerifier::process() - Bad EFM sector 55000 found in file DATA1 ADFS sector 0x06B120
Warning: AdfsVerifier::process() - Bad EFM sector 55002 found in file DATA1 ADFS sector 0x06B130
Warning: AdfsVerifier::process() - Bad EFM sector 55005 found in file DATA1 ADFS sector 0x06B148
Warning: AdfsVerifier::process() - Bad EFM sector 55008 found in file DATA1 ADFS sector 0x06B160
Warning: AdfsVerifier::process() - Bad EFM sector 55009 found in file DATA1 ADFS sector 0x06B168
Warning: AdfsVerifier::process() - Bad EFM sector 55011 found in file DATA1 ADFS sector 0x06B178
Warning: AdfsVerifier::process() - Bad EFM sector 55012 found in file DATA1 ADFS sector 0x06B180
Warning: AdfsVerifier::process() - Bad EFM sector 55014 found in file DATA1 ADFS sector 0x06B190
Warning: AdfsVerifier::process() - Bad EFM sector 55015 found in file DATA1 ADFS sector 0x06B198
Warning: AdfsVerifier::process() - Bad EFM sector 55017 found in file DATA1 ADFS sector 0x06B1A8
Warning: AdfsVerifier::process() - Bad EFM sector 55018 found in file DATA1 ADFS sector 0x06B1B0
Warning: AdfsVerifier::process() - Bad EFM sector 55020 found in file DATA1 ADFS sector 0x06B1C0
Warning: AdfsVerifier::process() - Bad EFM sector 55021 found in file DATA1 ADFS sector 0x06B1C8
Warning: AdfsVerifier::process() - Bad EFM sector 55026 found in file DATA1 ADFS sector 0x06B1F0
Warning: AdfsVerifier::process() - Bad EFM sector 55027 found in file DATA1 ADFS sector 0x06B1F8
Warning: AdfsVerifier::process() - Bad EFM sector 55032 found in file DATA1 ADFS sector 0x06B220
Warning: AdfsVerifier::process() - Bad EFM sector 55033 found in file DATA1 ADFS sector 0x06B228
Warning: AdfsVerifier::process() - Bad EFM sector 55035 found in file DATA1 ADFS sector 0x06B238
Warning: AdfsVerifier::process() - Bad EFM sector 55036 found in file DATA1 ADFS sector 0x06B240
Warning: AdfsVerifier::process() - Bad EFM sector 55038 found in file DATA1 ADFS sector 0x06B250
Warning: AdfsVerifier::process() - Bad EFM sector 55039 found in file DATA1 ADFS sector 0x06B258
Warning: AdfsVerifier::process() - Bad EFM sector 86074 found in file DATA2 ADFS sector 0x0A7C30
Warning: AdfsVerifier::process() - Bad EFM sector 86075 found in file DATA2 ADFS sector 0x0A7C38
Warning: AdfsVerifier::process() - Bad EFM sector 88141 found in file DATA2 ADFS sector 0x0ABCC8
Warning: AdfsVerifier::process() - Bad EFM sector 88142 found in file DATA2 ADFS sector 0x0ABCD0
Warning: AdfsVerifier::process() - Bad EFM sector 88437 found in file DATA2 ADFS sector 0x0AC608
Warning: AdfsVerifier::process() - Bad EFM sector 88438 found in file DATA2 ADFS sector 0x0AC610
Warning: AdfsVerifier::process() - Bad EFM sector 90254 found in file DATA2 ADFS sector 0x0AFED0
Warning: AdfsVerifier::process() - Bad EFM sector 90255 found in file DATA2 ADFS sector 0x0AFED8
Warning: AdfsVerifier::process() - Bad EFM sector 90543 found in file DATA2 ADFS sector 0x0B07D8
Warning: AdfsVerifier::process() - Bad EFM sector 90544 found in file DATA2 ADFS sector 0x0B07E0
Warning: AdfsVerifier::process() - Bad EFM sector 90835 found in file DATA2 ADFS sector 0x0B10F8
Warning: AdfsVerifier::process() - Bad EFM sector 90836 found in file DATA2 ADFS sector 0x0B1100
Warning: AdfsVerifier::process() - Bad EFM sector 93467 found in file DATA2 ADFS sector 0x0B6338
Warning: AdfsVerifier::process() - Bad EFM sector 93468 found in file DATA2 ADFS sector 0x0B6340
Warning: AdfsVerifier::process() - Bad EFM sector 93470 found in file DATA2 ADFS sector 0x0B6350
Warning: AdfsVerifier::process() - Bad EFM sector 93471 found in file DATA2 ADFS sector 0x0B6358
Warning: AdfsVerifier::process() - Bad EFM sector 94287 found in file DATA2 ADFS sector 0x0B7CD8
Warning: AdfsVerifier::process() - Bad EFM sector 94288 found in file DATA2 ADFS sector 0x0B7CE0
Warning: AdfsVerifier::process() - Bad EFM sector 94289 found in file DATA2 ADFS sector 0x0B7CE8
Warning: AdfsVerifier::process() - Bad EFM sector 94290 found in file DATA2 ADFS sector 0x0B7CF0
Warning: AdfsVerifier::process() - Bad EFM sector 94609 found in file DATA2 ADFS sector 0x0B86E8
Warning: AdfsVerifier::process() - Bad EFM sector 94610 found in file DATA2 ADFS sector 0x0B86F0
Warning: AdfsVerifier::process() - Bad EFM sector 95274 found in file DATA2 ADFS sector 0x0B9BB0
Warning: AdfsVerifier::process() - Bad EFM sector 95275 found in file DATA2 ADFS sector 0x0B9BB8
Warning: AdfsVerifier::process() - Bad EFM sector 95898 found in file DATA2 ADFS sector 0x0BAF30
Warning: AdfsVerifier::process() - Bad EFM sector 95899 found in file DATA2 ADFS sector 0x0BAF38
Warning: AdfsVerifier::process() - Bad EFM sector 95993 found in file DATA2 ADFS sector 0x0BB228
Warning: AdfsVerifier::process() - Bad EFM sector 95994 found in file DATA2 ADFS sector 0x0BB230
Warning: AdfsVerifier::process() - Bad EFM sector 96267 found in file DATA2 ADFS sector 0x0BBAB8
Warning: AdfsVerifier::process() - Bad EFM sector 96268 found in file DATA2 ADFS sector 0x0BBAC0
Warning: AdfsVerifier::process() - Bad EFM sector 96270 found in file DATA2 ADFS sector 0x0BBAD0
Warning: AdfsVerifier::process() - Bad EFM sector 96271 found in file DATA2 ADFS sector 0x0BBAD8
Warning: AdfsVerifier::process() - Bad EFM sector 96561 found in file DATA2 ADFS sector 0x0BC3E8
Warning: AdfsVerifier::process() - Bad EFM sector 96562 found in file DATA2 ADFS sector 0x0BC3F0
Warning: AdfsVerifier::process() - Bad EFM sector 96622 found in file DATA2 ADFS sector 0x0BC5D0
Warning: AdfsVerifier::process() - Bad EFM sector 96623 found in file DATA2 ADFS sector 0x0BC5D8
Warning: AdfsVerifier::process() - Bad EFM sector 96631 found in file DATA2 ADFS sector 0x0BC618
Warning: AdfsVerifier::process() - Bad EFM sector 96632 found in file DATA2 ADFS sector 0x0BC620
Warning: AdfsVerifier::process() - Bad EFM sector 96634 found in file DATA2 ADFS sector 0x0BC630
Warning: AdfsVerifier::process() - Bad EFM sector 96635 found in file DATA2 ADFS sector 0x0BC638
Warning: AdfsVerifier::process() - Bad EFM sector 97045 found in file DATA2 ADFS sector 0x0BD308
Warning: AdfsVerifier::process() - Bad EFM sector 97046 found in file DATA2 ADFS sector 0x0BD310
Warning: AdfsVerifier::process() - Bad EFM sector 97047 found in file DATA2 ADFS sector 0x0BD318
Warning: AdfsVerifier::process() - Bad EFM sector 98362 found in file DATA2 ADFS sector 0x0BFC30
Warning: AdfsVerifier::process() - Bad EFM sector 98363 found in file DATA2 ADFS sector 0x0BFC38
Warning: AdfsVerifier::process() - Bad EFM sector 98611 found in file DATA2 ADFS sector 0x0C03F8
Warning: AdfsVerifier::process() - Bad EFM sector 98612 found in file DATA2 ADFS sector 0x0C0400
Warning: AdfsVerifier::process() - Bad EFM sector 99198 found in file DATA2 ADFS sector 0x0C1650
Warning: AdfsVerifier::process() - Bad EFM sector 99199 found in file DATA2 ADFS sector 0x0C1658
Warning: AdfsVerifier::process() - Bad EFM sector 99201 found in file DATA2 ADFS sector 0x0C1668
Warning: AdfsVerifier::process() - Bad EFM sector 99202 found in file DATA2 ADFS sector 0x0C1670
Warning: AdfsVerifier::process() - Bad EFM sector 99246 found in file DATA2 ADFS sector 0x0C17D0
Warning: AdfsVerifier::process() - Bad EFM sector 99247 found in file DATA2 ADFS sector 0x0C17D8
Warning: AdfsVerifier::process() - Bad EFM sector 99548 found in file DATA2 ADFS sector 0x0C2140
Warning: AdfsVerifier::process() - Bad EFM sector 99549 found in file DATA2 ADFS sector 0x0C2148
Warning: AdfsVerifier::process() - Bad EFM sector 99814 found in file DATA2 ADFS sector 0x0C2990
Warning: AdfsVerifier::process() - Bad EFM sector 99815 found in file DATA2 ADFS sector 0x0C2998
Warning: AdfsVerifier::process() - Bad EFM sector 101985 found in file DATA2 ADFS sector 0x0C6D68
Warning: AdfsVerifier::process() - Bad EFM sector 102593 found in file DATA2 ADFS sector 0x0C8068
Warning: AdfsVerifier::process() - Bad EFM sector 102594 found in file DATA2 ADFS sector 0x0C8070
Warning: AdfsVerifier::process() - Bad EFM sector 103023 found in file DATA2 ADFS sector 0x0C8DD8
Warning: AdfsVerifier::process() - Bad EFM sector 103024 found in file DATA2 ADFS sector 0x0C8DE0
Warning: AdfsVerifier::process() - Bad EFM sector 103145 found in file DATA2 ADFS sector 0x0C91A8
Warning: AdfsVerifier::process() - Bad EFM sector 103146 found in file DATA2 ADFS sector 0x0C91B0
Warning: AdfsVerifier::process() - Bad EFM sector 105077 found in file DATA2 ADFS sector 0x0CCE08
Warning: AdfsVerifier::process() - Bad EFM sector 105078 found in file DATA2 ADFS sector 0x0CCE10
Warning: AdfsVerifier::process() - Bad EFM sector 105216 found in file DATA2 ADFS sector 0x0CD260
Warning: AdfsVerifier::process() - Bad EFM sector 105217 found in file DATA2 ADFS sector 0x0CD268
Warning: AdfsVerifier::process() - Bad EFM sector 105543 found in file DATA2 ADFS sector 0x0CDC98
Warning: AdfsVerifier::process() - Bad EFM sector 105544 found in file DATA2 ADFS sector 0x0CDCA0
Warning: AdfsVerifier::process() - Bad EFM sector 105603 found in file DATA2 ADFS sector 0x0CDE78
Warning: AdfsVerifier::process() - Bad EFM sector 105604 found in file DATA2 ADFS sector 0x0CDE80
Warning: AdfsVerifier::process() - Bad EFM sector 105950 found in file DATA2 ADFS sector 0x0CE950
Warning: AdfsVerifier::process() - Bad EFM sector 105951 found in file DATA2 ADFS sector 0x0CE958
Warning: AdfsVerifier::process() - Bad EFM sector 106631 found in file DATA2 ADFS sector 0x0CFE98
Warning: AdfsVerifier::process() - Bad EFM sector 106632 found in file DATA2 ADFS sector 0x0CFEA0
Warning: AdfsVerifier::process() - Bad EFM sector 106634 found in file DATA2 ADFS sector 0x0CFEB0
Warning: AdfsVerifier::process() - Bad EFM sector 106635 found in file DATA2 ADFS sector 0x0CFEB8
Warning: AdfsVerifier::process() - Bad EFM sector 106688 found in file DATA2 ADFS sector 0x0D0060
Warning: AdfsVerifier::process() - Bad EFM sector 106689 found in file DATA2 ADFS sector 0x0D0068
Warning: AdfsVerifier::process() - Bad EFM sector 107477 found in file DATA2 ADFS sector 0x0D1908
Warning: AdfsVerifier::process() - Bad EFM sector 108123 found in file DATA2 ADFS sector 0x0D2D38
Warning: AdfsVerifier::process() - Bad EFM sector 108124 found in file DATA2 ADFS sector 0x0D2D40
Warning: AdfsVerifier::process() - Bad EFM sector 112401 found in file DATA2 ADFS sector 0x0DB2E8
Warning: AdfsVerifier::process() - Bad EFM sector 112402 found in file DATA2 ADFS sector 0x0DB2F0
Warning: AdfsVerifier::process() - Bad EFM sector 112729 found in file DATA2 ADFS sector 0x0DBD28
Warning: AdfsVerifier::process() - Bad EFM sector 113671 found in file DATA2 ADFS sector 0x0DDA98
Warning: AdfsVerifier::process() - Bad EFM sector 113672 found in file DATA2 ADFS sector 0x0DDAA0
Warning: AdfsVerifier::process() - Bad EFM sector 114628 found in file DATA2 ADFS sector 0x0DF880
Warning: AdfsVerifier::process() - Bad EFM sector 114629 found in file DATA2 ADFS sector 0x0DF888
Warning: AdfsVerifier::process() - Bad EFM sector 115554 found in file DATA2 ADFS sector 0x0E1570
Warning: AdfsVerifier::process() - Bad EFM sector 115555 found in file DATA2 ADFS sector 0x0E1578
Warning: AdfsVerifier::process() - Bad EFM sector 116376 found in file DATA2 ADFS sector 0x0E2F20
Warning: AdfsVerifier::process() - Bad EFM sector 116377 found in file DATA2 ADFS sector 0x0E2F28
Warning: AdfsVerifier::process() - Bad EFM sector 117388 found in file DATA2 ADFS sector 0x0E4EC0
Warning: AdfsVerifier::process() - Bad EFM sector 117389 found in file DATA2 ADFS sector 0x0E4EC8
Warning: AdfsVerifier::process() - Bad EFM sector 118093 found in file DATA2 ADFS sector 0x0E64C8
Warning: AdfsVerifier::process() - Bad EFM sector 118094 found in file DATA2 ADFS sector 0x0E64D0
Warning: AdfsVerifier::process() - Bad EFM sector 118539 found in file DATA2 ADFS sector 0x0E72B8
Warning: AdfsVerifier::process() - Bad EFM sector 118540 found in file DATA2 ADFS sector 0x0E72C0
Warning: AdfsVerifier::process() - Bad EFM sector 118812 found in file DATA2 ADFS sector 0x0E7B40
Warning: AdfsVerifier::process() - Bad EFM sector 118813 found in file DATA2 ADFS sector 0x0E7B48
Warning: AdfsVerifier::process() - Bad EFM sector 119304 found in file DATA2 ADFS sector 0x0E8AA0
Warning: AdfsVerifier::process() - Bad EFM sector 119305 found in file DATA2 ADFS sector 0x0E8AA8
Warning: AdfsVerifier::process() - Bad EFM sector 119310 found in file DATA2 ADFS sector 0x0E8AD0
Warning: AdfsVerifier::process() - Bad EFM sector 119311 found in file DATA2 ADFS sector 0x0E8AD8
Warning: AdfsVerifier::process() - Bad EFM sector 119347 found in file DATA2 ADFS sector 0x0E8BF8
Warning: AdfsVerifier::process() - Bad EFM sector 119348 found in file DATA2 ADFS sector 0x0E8C00
Warning: AdfsVerifier::process() - Bad EFM sector 119583 found in file DATA2 ADFS sector 0x0E9358
Warning: AdfsVerifier::process() - Bad EFM sector 119584 found in file DATA2 ADFS sector 0x0E9360
Warning: AdfsVerifier::process() - Bad EFM sector 121128 found in file DATA2 ADFS sector 0x0EC3A0
Warning: AdfsVerifier::process() - Bad EFM sector 121129 found in file DATA2 ADFS sector 0x0EC3A8
Warning: AdfsVerifier::process() - Bad EFM sector 121625 found in file DATA2 ADFS sector 0x0ED328
Warning: AdfsVerifier::process() - Bad EFM sector 121626 found in file DATA2 ADFS sector 0x0ED330
Warning: AdfsVerifier::process() - Bad EFM sector 122448 found in file DATA2 ADFS sector 0x0EECE0
Warning: AdfsVerifier::process() - Bad EFM sector 122449 found in file DATA2 ADFS sector 0x0EECE8
Warning: AdfsVerifier::process() - Bad EFM sector 122460 found in file DATA2 ADFS sector 0x0EED40
Warning: AdfsVerifier::process() - Bad EFM sector 122461 found in file DATA2 ADFS sector 0x0EED48
Warning: AdfsVerifier::process() - Bad EFM sector 122709 found in file DATA2 ADFS sector 0x0EF508
Warning: AdfsVerifier::process() - Bad EFM sector 122710 found in file DATA2 ADFS sector 0x0EF510
Warning: AdfsVerifier::process() - Bad EFM sector 122839 found in file DATA2 ADFS sector 0x0EF918
Warning: AdfsVerifier::process() - Bad EFM sector 122840 found in file DATA2 ADFS sector 0x0EF920
Warning: AdfsVerifier::process() - Bad EFM sector 122854 found in file DATA2 ADFS sector 0x0EF990
Warning: AdfsVerifier::process() - Bad EFM sector 122855 found in file DATA2 ADFS sector 0x0EF998
Warning: AdfsVerifier::process() - Bad EFM sector 122860 found in file DATA2 ADFS sector 0x0EF9C0
Warning: AdfsVerifier::process() - Bad EFM sector 122869 found in file DATA2 ADFS sector 0x0EFA08
Warning: AdfsVerifier::process() - Bad EFM sector 122870 found in file DATA2 ADFS sector 0x0EFA10
Warning: AdfsVerifier::process() - Bad EFM sector 122893 found in file DATA2 ADFS sector 0x0EFAC8
Warning: AdfsVerifier::process() - Bad EFM sector 122894 found in file DATA2 ADFS sector 0x0EFAD0
Warning: AdfsVerifier::process() - Bad EFM sector 122896 found in file DATA2 ADFS sector 0x0EFAE0
Warning: AdfsVerifier::process() - Bad EFM sector 122897 found in file DATA2 ADFS sector 0x0EFAE8
Warning: AdfsVerifier::process() - Bad EFM sector 124041 found in file DATA2 ADFS sector 0x0F1EA8
Warning: AdfsVerifier::process() - Bad EFM sector 124042 found in file DATA2 ADFS sector 0x0F1EB0
Warning: AdfsVerifier::process() - Bad EFM sector 64487 found in file GAZETTEER ADFS sector 0x07D998
Warning: AdfsVerifier::process() - Bad EFM sector 64488 found in file GAZETTEER ADFS sector 0x07D9A0
Warning: AdfsVerifier::process() - Bad EFM sector 64785 found in file GAZETTEER ADFS sector 0x07E2E8
Warning: AdfsVerifier::process() - Bad EFM sector 65575 found in file GAZETTEER ADFS sector 0x07FB98
Warning: AdfsVerifier::process() - Bad EFM sector 65576 found in file GAZETTEER ADFS sector 0x07FBA0
Warning: AdfsVerifier::process() - Bad EFM sector 65584 found in file GAZETTEER ADFS sector 0x07FBE0
Warning: AdfsVerifier::process() - Bad EFM sector 65585 found in file GAZETTEER ADFS sector 0x07FBE8
Warning: AdfsVerifier::process() - Bad EFM sector 66683 found in file GAZETTEER ADFS sector 0x081E38
Warning: AdfsVerifier::process() - Bad EFM sector 66684 found in file GAZETTEER ADFS sector 0x081E40
Warning: AdfsVerifier::process() - Bad EFM sector 67050 found in file GAZETTEER ADFS sector 0x0829B0
Warning: AdfsVerifier::process() - Bad EFM sector 67487 found in file GAZETTEER ADFS sector 0x083758
Warning: AdfsVerifier::process() - Bad EFM sector 67488 found in file GAZETTEER ADFS sector 0x083760
Warning: AdfsVerifier::process() - Bad EFM sector 67505 found in file GAZETTEER ADFS sector 0x0837E8
Warning: AdfsVerifier::process() - Bad EFM sector 67506 found in file GAZETTEER ADFS sector 0x0837F0
Warning: AdfsVerifier::process() - Bad EFM sector 67621 found in file GAZETTEER ADFS sector 0x083B88
Warning: AdfsVerifier::process() - Bad EFM sector 67622 found in file GAZETTEER ADFS sector 0x083B90
Warning: AdfsVerifier::process() - Bad EFM sector 59377 found in file INDEX ADFS sector 0x0739E8
Warning: AdfsVerifier::process() - Bad EFM sector 59378 found in file INDEX ADFS sector 0x0739F0
Warning: AdfsVerifier::process() - Bad EFM sector 59423 found in file INDEX ADFS sector 0x073B58
Warning: AdfsVerifier::process() - Bad EFM sector 59424 found in file INDEX ADFS sector 0x073B60
Warning: AdfsVerifier::process() - Bad EFM sector 59769 found in file INDEX ADFS sector 0x074628
Warning: AdfsVerifier::process() - Bad EFM sector 59770 found in file INDEX ADFS sector 0x074630
Warning: AdfsVerifier::process() - Bad EFM sector 59943 found in file INDEX ADFS sector 0x074B98
Warning: AdfsVerifier::process() - Bad EFM sector 59944 found in file INDEX ADFS sector 0x074BA0
Warning: AdfsVerifier::process() - Bad EFM sector 61292 found in file INDEX ADFS sector 0x0775C0
Warning: AdfsVerifier::process() - Bad EFM sector 61293 found in file INDEX ADFS sector 0x0775C8
Warning: AdfsVerifier::process() - Bad EFM sector 61824 found in file NAMES ADFS sector 0x078660
Warning: AdfsVerifier::process() - Bad EFM sector 61825 found in file NAMES ADFS sector 0x078668
Warning: AdfsVerifier::process() - Bad EFM sector 62803 found in file NAMES ADFS sector 0x07A4F8
Warning: AdfsVerifier::process() - Bad EFM sector 63588 found in file NAMES ADFS sector 0x07BD80
Warning: AdfsVerifier::process() - Bad EFM sector 63589 found in file NAMES ADFS sector 0x07BD88
Info: AdfsVerifier::process() - Verification failed - 350 bad sectors found in VFS image file "DS2_comS1.bin"

