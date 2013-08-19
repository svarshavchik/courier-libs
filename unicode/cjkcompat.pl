##
## Maps for compatibility ideographs.
## 

# Some standards have separated Uniocde maps for same ideographs
# by each specific reasons.  Those separations are not meaningful
# for other standards.  So they must be unified.
# cf. Unicode Standard.

## CJK Compatibility Ideographs (U+F900 - U+FAFF)

# Pronounciation variants from KS X 1001:1998
%compat_ksx1001 = (
	# 42-25 ga
	0xF903 => 0x8CC8, # 45-47 go
	# 43-29 gang
	0xFA09 => 0x964D, # 90-02 hang
	# 49-34 gi
	0xF900 => 0x8C48, # 43-48 gae
	# 44-58 gyeong
	0xF901 => 0x66F4, # 43-54 gaeng
	# 83-19 ca
	0xF902 => 0x8ECA, # 43-71 geo
	# 44-24 gyeon
	0xFA0A => 0x898B, # 90-70 hyeon
	# 44-88 gye
	0xF909 => 0x5951, # 48-48 geul
	# 92-33 hwal
	0xF904 => 0x6ED1, # 45-72 gol
	# 45-90 goj
	0xF905 => 0x4E32, # 46-13 gwan
	# 46-09 gwag
	0xFA0B => 0x5ED3, # 92-09 hwag
	# 47-03 gu
	0xF906 => 0x53E5, # 47-91 gwi
	# 47-47 gu
	0xF907 => 0x9F9C, # 48-02 gwi
	0xF908 => 0x9F9C, # 48-24 gyun
	# 49-49 gim
	0xF90A => 0x91D1, # 48-61 geum
	# 52-90 ra
	0xF90B => 0x5587, # 49-52 na
	# 50-15 nae
	0xF90C => 0x5948, # 49-53 na
	# 52-91 ra
	0xF90D => 0x61F6, # 49-56 na
	# 49-57 na
	0xF95B => 0x62CF, # 52-92 ra
	# 52-93 ra
	0xF90E => 0x7669, # 49-59 na
	# 52-94 ra
	0xF90F => 0x7F85, # 49-60 na
	# 53-01 ra
	0xF910 => 0x863F, # 49-61 na
	# 53-02 ra
	0xF911 => 0x87BA, # 49-62 na
	# 53-03 ra
	0xF912 => 0x88F8, # 49-63 na
	# 53-04 ra
	0xF913 => 0x908F, # 49-64 na
	# 68-37 ag
	0xF914 => 0x6A02, # 49-66 nag
	0xF95C => 0x6A02, # 53-05 rag
	0xF9BF => 0x6A02, # 72-89 yo
	# 53-06 rag
	0xF915 => 0x6D1B, # 49-67 nag
	# 53-07 rag
	0xF916 => 0x70D9, # 49-68 nag
	# 53-08 rag
	0xF917 => 0x73DE, # 49-69 nag
	# 53-10 rag
	0xF918 => 0x843D, # 49-70 nag
	# 49-71 nag
	0xF95D => 0x8AFE, # 53-11 rag
	# 53-12 rag
	0xF919 => 0x916A, # 49-72 nag
	# 53-13 rag
	0xF91A => 0x99F1, # 49-73 nag
	# 53-15 ran
	0xF91B => 0x4E82, # 49-74 nan
	# 53-16 ran
	0xF91C => 0x5375, # 49-75 nan
	# 53-17 ran
	0xF91D => 0x6B04, # 49-77 nan
	# 53-20 ran
	0xF91E => 0x721B, # 49-79 nan
	# 53-21 ran
	0xF91F => 0x862D, # 49-80 nan
	# 53-22 ran
	0xF920 => 0x9E1E, # 49-82 nan
	# 53-25 ram
	0xF921 => 0x5D50, # 49-86 nam
	# 53-29 ram
	0xF922 => 0x6FEB, # 49-90 nam
	# 53-32 ram
	0xF923 => 0x85CD, # 49-92 nam
	# 53-33 ram
	0xF924 => 0x8964, # 49-93 nam
	# 53-35 rab
	0xF925 => 0x62C9, # 49-94 nab
	# 53-36 rab
	0xF926 => 0x81D8, # 50-02 nab
	# 53-37 rab
	0xF927 => 0x881F, # 50-03 nab
	# 53-38 rang
	0xF928 => 0x5ECA, # 50-07 nang
	# 53-39 rang
	0xF929 => 0x6717, # 50-08 nang
	# 53-40 rang
	0xF92A => 0x6D6A, # 50-09 nang
	# 53-41 rang
	0xF92B => 0x72FC, # 50-10 nang
	# 53-45 rang
	0xF92C => 0x90DE, # 50-11 nang
	# 53-46 rae
	0xF92D => 0x4F86, # 50-13 nae
	# 53-50 raeng
	0xF92E => 0x51B7, # 50-18 naeng
	# 50-19 nyeo
	0xF981 => 0x5973, # 69-92 yeo
	# 50-20 nyeon
	0xF98E => 0x5E74, # 70-36 yeon
	# 50-21 nyeon
	0xF991 => 0x649A, # 70-42 yeon
	# 50-22 nyeon
	0xF995 => 0x79CA, # 70-60 yeon
	# 50-23 nyeom
	0xF9A3 => 0x5FF5, # 70-86 yeom
	# 50-26 nyeom
	0xF9A4 => 0x637B, # 70-87 yeom
	# 50-27 nyeong
	0xF95F => 0x5BE7, # 54-24 ryeong
	0xF9AA => 0x5BE7, # 71-12 yeong
	# 54-44 ro
	0xF92F => 0x52DE, # 50-30 no
	# 50-33 no
	0xF960 => 0x6012, # 54-45 ro
	# 54-47 ro
	0xF930 => 0x64C4, # 50-34 no
	# 54-48 ro
	0xF931 => 0x6AD3, # 50-35 no
	# 54-51 ro
	0xF932 => 0x7210, # 50-36 no
	# 54-52 ro
	0xF933 => 0x76E7, # 50-38 no
	# 54-53 ro
	0xF934 => 0x8001, # 50-39 no
	# 54-54 ro
	0xF935 => 0x8606, # 50-40 no
	# 54-55 ro
	0xF936 => 0x865C, # 50-41 no
	# 54-56 ro
	0xF937 => 0x8DEF, # 50-42 no
	# 54-58 ro
	0xF938 => 0x9732, # 50-43 no
	# 54-59 ro
	0xF939 => 0x9B6F, # 50-45 no
	# 54-60 ro
	0xF93A => 0x9DFA, # 50-46 no
	# 54-62 rog
	0xF93B => 0x788C, # 50-47 nog
	# 54-63 rog
	0xF93C => 0x797F, # 50-48 nog
	# 54-64 rog
	0xF93D => 0x7DA0, # 50-49 nog
	# 54-65 rog
	0xF93E => 0x83C9, # 50-50 nog
	# 54-66 rog
	0xF93F => 0x9304, # 50-51 nog
	# 54-67 rog
	0xF940 => 0x9E7F, # 50-52 nog
	# 54-69 ron
	0xF941 => 0x8AD6, # 50-53 non
	# 54-70 rong
	0xF942 => 0x58DF, # 50-54 nong
	# 54-71 rong
	0xF943 => 0x5F04, # 50-55 nong
	# 54-75 rong
	0xF944 => 0x7C60, # 50-57 nong
	# 54-76 rong
	0xF945 => 0x807E, # 50-58 nong
	# 54-79 roe
	0xF946 => 0x7262, # 50-62 noe
	# 54-80 roe
	0xF947 => 0x78CA, # 50-63 noe
	# 54-81 roe
	0xF948 => 0x8CC2, # 50-65 noe
	# 54-84 roe
	0xF949 => 0x96F7, # 50-66 noe
	# 50-67 nyo
	0xF9BD => 0x5C3F, # 72-81 yo
	# 55-04 ru
	0xF94A => 0x58D8, # 50-68 nu
	# 55-06 ru
	0xF94B => 0x5C62, # 50-69 nu
	# 55-07 ru
	0xF94C => 0x6A13, # 50-70 nu
	# 55-08 ru
	0xF94D => 0x6DDA, # 50-71 nu
	# 55-09 ru
	0xF94E => 0x6F0F, # 50-72 nu
	# 55-11 ru
	0xF94F => 0x7D2F, # 50-73 nu
	# 55-12 ru
	0xF950 => 0x7E37, # 50-74 nu
	# 55-16 ru
	0xF951 => 0x964B, # 50-75 nu
	# 50-78 nyu
	0xF9C8 => 0x677B, # 74-84 yu
	# 50-79 nyu
	0xF9CF => 0x7D10, # 75-10 yu
	# 55-45 reug
	0xF952 => 0x52D2, # 50-80 neug
	# 55-46 reug
	0xF953 => 0x808B, # 50-81 neug
	# 55-47 reum
	0xF954 => 0x51DC, # 50-82 neum
	# 55-48 reung
	0xF955 => 0x51CC, # 50-83 neung
	# 55-50 reung
	0xF956 => 0x7A1C, # 50-84 neung
	# 55-51 reung
	0xF957 => 0x7DBE, # 50-85 neung
	# 55-52 reung
	0xF958 => 0x83F1, # 50-87 neung
	# 55-53 reung
	0xF959 => 0x9675, # 50-88 neung
	# 50-90 ni
	0xF9E3 => 0x6CE5, # 76-18 i
	# 50-91 nig
	0xF9EB => 0x533F, # 76-41 ig
	# 50-92 nig
	0xF9EC => 0x6EBA, # 76-42 ig
	# 50-94 da
	0xF9FE => 0x8336, # 83-17 ca
	# 51-01 dan
	0xF95E => 0x4E39, # 53-14 ran
	# 51-56 dang
	0xFA03 => 0x7CD6, # 87-24 tang
	# 51-75 daeg
	0xFA04 => 0x5B85, # 87-40 taeg
	# 51-88 do
	0xFA01 => 0x5EA6, # 86-84 tag
	# 52-33 dog
	0xF95A => 0x8B80, # 52-70 du
	# 52-55 dong
	0xFA05 => 0x6D1E, # 87-51 tong
	# 53-51 ryag
	0xF975 => 0x63A0, # 69-17 yag
	# 53-52 ryag
	0xF976 => 0x7565, # 69-18 yag
	# 53-53 ryang
	0xF977 => 0x4EAE, # 69-25 yang
	# 53-55 ryang
	0xF978 => 0x5169, # 69-27 yang
	# 53-56 ryang
	0xF979 => 0x51C9, # 69-28 yang
	# 53-57 ryang
	0xF97A => 0x6881, # 69-36 yang
	# 53-61 ryang
	0xF97B => 0x7CE7, # 69-46 yang
	# 53-62 ryang
	0xF97C => 0x826F, # 69-48 yang
	# 53-63 ryang
	0xF97D => 0x8AD2, # 69-50 yang
	# 53-65 ryang
	0xF97E => 0x91CF, # 69-54 yang
	# 53-68 ryeo
	0xF97F => 0x52F5, # 69-90 yeo
	# 53-69 ryeo
	0xF980 => 0x5442, # 69-91 yeo
	# 53-70 ryeo
	0xF982 => 0x5EEC, # 69-94 yeo
	# 53-73 ryeo
	0xF983 => 0x65C5, # 70-01 yeo
	# 53-75 ryeo
	0xF984 => 0x6FFE, # 70-04 yeo
	# 53-76 ryeo
	0xF985 => 0x792A, # 70-07 yeo
	# 53-79 ryeo
	0xF986 => 0x95AD, # 70-13 yeo
	# 53-81 ryeo
	0xF987 => 0x9A6A, # 70-15 yeo
	# 53-82 ryeo
	0xF988 => 0x9E97, # 70-16 yeo
	# 53-83 ryeo
	0xF989 => 0x9ECE, # 70-17 yeo
	# 53-84 ryeog
	0xF98A => 0x529B, # 70-19 yeog
	# 53-85 ryeog
	0xF98B => 0x66C6, # 70-23 yeog
	# 53-86 ryeog
	0xF98C => 0x6B77, # 70-24 yeog
	# 53-89 ryeog
	0xF98D => 0x8F62, # 70-28 yeog
	# 53-91 ryeon
	0xF98F => 0x6190, # 70-38 yeon
	# 53-92 ryeon
	0xF990 => 0x6200, # 70-39 yeon
	# 53-94 ryeon
	0xF992 => 0x6F23, # 70-50 yeon
	# 54-01 ryeon
	0xF993 => 0x7149, # 70-54 yeon
	# 54-02 ryeon
	0xF994 => 0x7489, # 70-57 yeon
	# 54-03 ryeon
	0xF996 => 0x7DF4, # 70-63 yeon
	# 54-04 ryeon
	0xF997 => 0x806F, # 70-65 yeon
	# 54-05 ryeon
	0xF999 => 0x84EE, # 70-69 yeon
	# 54-06 ryeon
	0xF998 => 0x8F26, # 70-68 yeon
	# 54-07 ryeon
	0xF99A => 0x9023, # 70-70 yeon
	# 54-08 ryeon
	0xF99B => 0x934A, # 70-72 yeon
	# 54-10 ryeol
	0xF99C => 0x5217, # 70-74 yeol
	# 54-11 ryeol
	0xF99D => 0x52A3, # 70-75 yeol
	# 54-13 ryeol
	0xF99F => 0x70C8, # 70-79 yeol
	# 54-14 ryeol
	0xF9A0 => 0x88C2, # 70-81 yeol
	# 54-15 ryeom
	0xF9A2 => 0x5EC9, # 70-85 yeom
	# 54-17 ryeom
	0xF9A5 => 0x6BAE, # 70-89 yeom
	# 54-19 ryeom
	0xF9A6 => 0x7C3E, # 71-01 yeom
	# 54-20 ryeob
	0xF9A7 => 0x7375, # 71-06 yeob
	# 54-21 ryeong
	0xF9A8 => 0x4EE4, # 71-09 yeong
	# 54-23 ryeong
	0xF9A9 => 0x56F9, # 71-10 yeong
	# 54-26 ryeong
	0xF9AB => 0x5DBA, # 71-13 yeong
	# 54-27 ryeong
	0xF9AC => 0x601C, # 71-16 yeong
	# 54-28 ryeong
	0xF9AD => 0x73B2, # 71-31 yeong
	# 54-30 ryeong
	0xF9AF => 0x7F9A, # 71-38 yeong
	# 54-32 ryeong
	0xF9B0 => 0x8046, # 71-39 yeong
	# 54-34 ryeong
	0xF9B1 => 0x9234, # 71-43 yeong
	# 54-35 ryeong
	0xF9B2 => 0x96F6, # 71-45 yeong
	# 54-36 ryeong
	0xF9B3 => 0x9748, # 71-47 yeong
	# 54-37 ryeong
	0xF9B4 => 0x9818, # 71-48 yeong
	# 54-39 rye
	0xF9B5 => 0x4F8B, # 71-51 ye
	# 54-41 rye
	0xF9B6 => 0x79AE, # 71-63 ye
	# 54-42 rye
	0xF9B7 => 0x91B4, # 71-68 ye
	# 54-43 rye
	0xF9B8 => 0x96B7, # 71-70 ye
	# 54-85 ryo
	0xF9BA => 0x4E86, # 72-71 yo
	# 54-86 ryo
	0xF9BB => 0x50DA, # 72-72 yo
	# 54-87 ryo
	0xF9BC => 0x5BEE, # 72-80 yo
	# 54-89 ryo
	0xF9BE => 0x6599, # 72-87 yo
	# 54-90 ryo
	0xF9C0 => 0x71CE, # 72-91 yo
	# 54-91 ryo
	0xF9C1 => 0x7642, # 72-94 yo
	# 54-94 ryo
	0xF9C2 => 0x84FC, # 73-07 yo
	# 55-01 ryo
	0xF9C3 => 0x907C, # 73-12 yo
	# 55-03 ryong
	0xF9C4 => 0x9F8D, # 73-44 yong
	# 55-17 ryu
	0xF9C7 => 0x5289, # 74-69 yu
	# 55-19 ryu
	0xF9C9 => 0x67F3, # 74-87 yu
	# 55-21 ryu
	0xF9CA => 0x6D41, # 74-92 yu
	# 55-22 ryu
	0xF9CB => 0x6E9C, # 74-94 yu
	# 55-24 ryu
	0xF9CC => 0x7409, # 75-04 yu
	# 55-26 ryu
	0xF9CD => 0x7559, # 75-07 yu
	# 55-28 ryu
	0xF9CE => 0x786B, # 75-09 yu
	# 55-30 ryu
	0xF9D0 => 0x985E, # 75-26 yu
	# 55-31 ryug
	0xF9D1 => 0x516D, # 75-27 yug
	# 55-32 ryug
	0xF9D2 => 0x622E, # 75-29 yug
	# 55-33 ryug
	0xF9D3 => 0x9678, # 75-33 yug
	# 55-35 ryun
	0xF9D4 => 0x502B, # 75-34 yun
	# 55-36 ryun
	0xF9D5 => 0x5D19, # 75-38 yun
	# 55-37 ryun
	0xF9D6 => 0x6DEA, # 75-39 yun
	# 55-39 ryun
	0xF9D7 => 0x8F2A, # 75-44 yun
	# 55-40 ryul
	0xF9D8 => 0x5F8B, # 75-47 yul
	# 55-41 ryul
	0xF9D9 => 0x6144, # 75-48 yul
	# 55-42 ryul
	0xF9DA => 0x6817, # 75-49 yul
	# 65-67 sol
	0xF961 => 0x7387, # 55-43 ryul
	0xF9DB => 0x7387, # 75-50 yul
	# 55-44 ryung
	0xF9DC => 0x9686, # 75-56 yung
	# 55-55 ri
	0xF9DD => 0x5229, # 76-06 i
	# 55-57 ri
	0xF9DE => 0x540F, # 76-07 i
	# 55-59 ri
	0xF9DF => 0x5C65, # 76-10 i
	# 55-61 ri
	0xF9E1 => 0x674E, # 76-16 i
	# 55-62 ri
	0xF9E2 => 0x68A8, # 76-17 i
	# 55-66 ri
	0xF9E4 => 0x7406, # 76-21 i
	# 76-22 i
	0xF962 => 0x7570, # 55-68 ri
	# 55-69 ri
	0xF9E5 => 0x75E2, # 76-24 i
	# 55-71 ri
	0xF9E6 => 0x7F79, # 76-26 i
	# 55-74 ri
	0xF9E7 => 0x88CF, # 76-32 i
	# 55-75 ri
	0xF9E8 => 0x88E1, # 76-33 i
	# 55-76 ri
	0xF9E9 => 0x91CC, # 76-37 i
	# 55-78 ri
	0xF9EA => 0x96E2, # 76-38 i
	# 55-80 rin
	0xF9ED => 0x541D, # 76-53 in
	# 55-82 rin
	0xF9EE => 0x71D0, # 76-61 in
	# 55-83 rin
	0xF9EF => 0x7498, # 76-62 in
	# 55-84 rin
	0xF9F0 => 0x85FA, # 76-65 in
	# 55-86 rin
	0xF9F1 => 0x96A3, # 76-68 in
	# 55-87 rin
	0xF9F2 => 0x9C57, # 76-71 in
	# 55-88 rin
	0xF9F3 => 0x9E9F, # 76-72 in
	# 55-89 rim
	0xF9F4 => 0x6797, # 76-87 im
	# 55-90 rim
	0xF9F5 => 0x6DCB, # 76-88 im
	# 55-92 rim
	0xF9F6 => 0x81E8, # 76-90 im
	# 56-01 rib
	0xF9F7 => 0x7ACB, # 77-01 ib
	# 56-02 rib
	0xF9F8 => 0x7B20, # 77-02 ib
	# 56-03 rib
	0xF9F9 => 0x7C92, # 77-03 ib
	# 58-82 ban
	0xF964 => 0x78FB, # 59-68 beon
	# 61-33 bug
	0xF963 => 0x5317, # 59-37 bae
	# 88-21 pyeon
	0xF965 => 0x4FBF, # 60-05 byeon
	# 60-54 bog
	0xF966 => 0x5FA9, # 61-05 bu
	# 60-63 bog
	0xFA07 => 0x8F3B, # 88-80 pog
	# 60-84 bu
	0xF967 => 0x4E0D, # 61-53 bul
	# 89-18 pil
	0xF968 => 0x6CCC, # 61-84 bi
	# 66-06 su
	0xF969 => 0x6578, # 62-92 sag
	# 63-67 saeg
	0xF96A => 0x7D22, # 62-94 sag
	# 63-15 sal
	0xF970 => 0x6BBA, # 65-77 swae
	# 83-49 cam
	0xF96B => 0x53C3, # 63-19 sam
	# 63-50 sang
	0xF9FA => 0x72C0, # 77-78 jang
	# 63-61 sae
	0xF96C => 0x585E, # 63-65 saeg
	# 64-93 seong
	0xF96D => 0x7701, # 63-72 saeng
	# 64-67 seol
	0xF96F => 0x8AAA, # 65-13 se
	0xF9A1 => 0x8AAA, # 70-82 yeol
	# 71-08 yeob
	0xF96E => 0x8449, # 64-81 seob
	# 67-06 seub
	0xF973 => 0x62FE, # 68-09 sib
	# 67-59 sig
	0xF9FC => 0x8B58, # 82-29 ji
	# 82-67 jin
	0xF971 => 0x8FB0, # 67-85 sin
	# 86-56 cim
	0xF972 => 0x6C88, # 68-01 sim
	# 68-07 sib
	0xF9FD => 0x4EC0, # 82-90 jib
	# 68-34 ag
	0xF9B9 => 0x60E1, # 71-87 o
	# 69-20 yag
	0xF974 => 0x82E5, # 69-14 ya
	# 70-22 yeog
	0xF9E0 => 0x6613, # 76-15 i
	# 76-54 in
	0xF99E => 0x54BD, # 70-76 yeol
	# 91-09 hyeong
	0xF9AE => 0x7469, # 71-33 yeong
	# 72-54 wan
	0xF9C6 => 0x962E, # 74-33 weon
	# 93-27 hun
	0xF9C5 => 0x6688, # 73-87 un
	# 77-09 ja
	0xF9FF => 0x523A, # 84-07 ceog
	# 77-19 ja
	0xF9FB => 0x7099, # 78-59 jeog
	# 79-23 jeol
	0xFA00 => 0x5207, # 84-78 ce
	# 84-12 ceog
	0xFA02 => 0x62D3, # 86-86 tag
	# 88-76 pog
	0xFA06 => 0x66B4, # 88-59 po
	# 90-28 haeng
	0xFA08 => 0x884C, # 90-01 hang
);

# Duplicates from Big 5
%compat_big5 = (
	0xFA0C => 0x5140,
	0xFA0D => 0x55C0,
);

# The IBM 32 compatibility ideograph
%compat_ibm32 = (
	0xFA10 => 0x585A,
	0xFA12 => 0x6674,
	0xFA15 => 0x51DE,
	0xFA16 => 0x732A,
	0xFA17 => 0x76CA,
	0xFA18 => 0x793C,
	0xFA19 => 0x795E,
	0xFA1A => 0x7965,
	0xFA1B => 0x798F,
	0xFA1C => 0x9756,
	0xFA1D => 0x7CBE,
	0xFA1E => 0x7FBD,
	0xFA20 => 0x8612,
	0xFA22 => 0x8AF8,
	0xFA25 => 0x9038,
	0xFA26 => 0x90FD,
	0xFA2A => 0x98EF,
	0xFA2B => 0x98FC,
	0xFA2C => 0x9928,
	0xFA2D => 0x9DB4,

);

# JIS X 0213 compatibility additions
%compat_jisx0213 = (
	0xFA30 => 0x4FAE,
	0xFA31 => 0x50E7,
	0xFA32 => 0x514D,
	0xFA33 => 0x52C9,
	0xFA34 => 0x52E4,
	0xFA35 => 0x5351,
	0xFA36 => 0x559D,
	0xFA37 => 0x5606,
	0xFA38 => 0x5668,
	0xFA39 => 0x5840,
	0xFA3A => 0x58A8,
	0xFA3B => 0x5C64,
	0xFA3C => 0x5C6E,
	0xFA3D => 0x6094,
	0xFA3E => 0x6168,
	0xFA3F => 0x618E,
	0xFA40 => 0x61F2,
	0xFA41 => 0x654F,
	0xFA42 => 0x65E2,
	0xFA43 => 0x6691,
	0xFA44 => 0x6885,
	0xFA45 => 0x6D77,
	0xFA46 => 0x6E1A,
	0xFA47 => 0x6F22,
	0xFA48 => 0x716E,
	0xFA49 => 0x722B,
	0xFA4A => 0x7422,
	0xFA4B => 0x7891,
	0xFA4C => 0x793E,
	0xFA4D => 0x7949,
	0xFA4E => 0x7948,
	0xFA4F => 0x7950,
	0xFA50 => 0x7956,
	0xFA51 => 0x795D,
	0xFA52 => 0x798D,
	0xFA53 => 0x798E,
	0xFA54 => 0x7A40,
	0xFA55 => 0x7A81,
	0xFA56 => 0x7BC0,
	0xFA57 => 0x7DF4,
	0xFA58 => 0x7E09,
	0xFA59 => 0x7E41,
	0xFA5A => 0x7F72,
	0xFA5B => 0x8005,
	0xFA5C => 0x81ED,
	0xFA5D => 0x8279,
	0xFA5E => 0x8279,
	0xFA5F => 0x8457,
	0xFA60 => 0x8910,
	0xFA61 => 0x8996,
	0xFA62 => 0x8B01,
	0xFA63 => 0x8B39,
	0xFA64 => 0x8CD3,
	0xFA65 => 0x8D08,
	0xFA66 => 0x8FB6,
	0xFA67 => 0x9038,
	0xFA68 => 0x96E3,
	0xFA69 => 0x97FF,
	0xFA6A => 0x983B,
);

## CJK Compatibility Ideographs Supplement (U+2F800 - U+2FA1F)

# Duplicate characters from CNS 11643-1992
%compat_cns11643 = (
	# By now we don't support CNS 11643.
);


1;

