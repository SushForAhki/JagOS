# JagOS v0.2 - Geliştirme Özeti

## Yeni Eklenen Özellikler

### 1. Türkçe Q Klavye Desteği
- **Dosya**: `kernel.c`
- Üç farklı klavye düzeni:
  - `US` - Standart QWERTY (varsayılan)
  - `TR-Q (ASCII)` - Türkçe Q layout, ASCII fallback
  - `TR-Q (UTF8)` - Türkçe Q layout, UTF-8 karakterler
- Türkçe karakterler: ğ, ü, ş, i, ö, ç, İ, Ğ, Ü, Ş, Ö, Ç
- Komut: `keyboard <us|tr|trutf8>`

### 2. CMOS/RTC - Tarih/Saat Desteği
- **Dosya**: `kernel.c`
- CMOS port I/O (0x70/0x71) entegrasyonu
- BCD-to-binary dönüşüm
- Yeni komutlar: `date`, `time`
- `sysinfo` komutunda gösterim

### 3. RAM Disk Dosya Sistemi
- **Dosya**: `shell.cpp`
- Bellek tabanlı dosya sistemi (16 dosya, her biri 1KB max)
- Yeni komutlar:
  - `ls` - Dosya listeleme
  - `pwd` - Mevcut dizin göster
  - `mkdir <dir>` - Dizin oluştur (stub)
  - `rm <file>` - Dosya sil
  - `cat <file>` - Dosya içeriği görüntüle
  - `write <file> <text>` - Dosya oluştur/yaz

### 4. Hesap Makinesi
- **Dosya**: `shell.cpp`
- Aritmetik işlemler: +, -, *, /
- Decimal ve hexadecimal (0x prefix) desteği
- Komut: `calc <expression>` (örn: `calc 10+5`, `calc 0xFF*2`)

### 5. Yeni Sistem Komutları
- **Dosyalar**: `kernel.c`, `shell.cpp`
- `reboot` - Sistemi yeniden başlat (8042/triple-fault)
- `shutdown` - Sistemi durdur
- `meminfo` - Bellek kullanım bilgisi (8MB heap)
- `serial <text>` - COM1 seri porta mesaj gönder (38400 baud)

### 6. Ortam Değişkenleri (Environment Variables)
- **Dosya**: `shell.cpp`
- 16 değişken kapasitesi
- Komutlar:
  - `env` - Değişkenleri listele
  - `set VAR=value` - Değişken tanımla
  - `unset VAR` - Değişken sil

### 7. Shell v2.0 Güncellemeleri
- **Dosya**: `shell.cpp`
- Yeni renkli help menü
- Kategorize edilmiş komut listesi:
  - [System] - Sistem komutları
  - [File System] - Dosya sistemi komutları
  - [Utilities] - Yardımcı komutlar
  - [Demo/Advanced] - Demo/ileri düzey komutlar

### 8. Seri Port Desteği
- **Dosya**: `kernel.c`
- COM1 (0x3F8) @ 38400 baud
- 8N1 format
- FIFO desteği

### 9. Bellek Yönetimi
- **Dosya**: `kernel.c`
- Basit bump allocator (kmalloc/kfree)
- 8MB heap alanı (0x100000 - 0x900000)
- Bellek kullanım takibi

## Değişen Dosyalar

| Dosya | Satır Sayısı | Değişiklik |
|-------|-------------|------------|
| `kernel.c` | ~570 | Türkçe klavye, CMOS, seri port, reboot, meminfo |
| `shell.cpp` | ~750 | RAM disk, env vars, hesap makinesi, yeni komutlar |

## Yeni Toplam Komut Sayısı: 25+

### Sistem Komutları
- `help`, `clear`, `sysinfo`, `reboot`, `shutdown`, `meminfo`, `keyboard`

### Dosya Sistemi
- `ls`, `pwd`, `mkdir`, `rm`, `cat`, `write`

### Yardımcı Komutlar
- `echo`, `color`, `date`, `time`, `calc`, `serial`, `env`, `set`, `unset`, `history`

### Demo/İleri Düzey
- `nubo`, `packet`, `jagu`, `panic`, `halt`

## Derleme Notları

```bash
# Gerekli araçlar: nasm, gcc, ld, python3
make clean
make all
make run  # QEMU ile çalıştırma
```

## Klavye Düzeni Değiştirme

```
jag@os:~$ keyboard tr
Keyboard layout set to: TR-Q (ASCII)

jag@os:~$ keyboard us
Keyboard layout set to: US (QWERTY)
```

## Örnek Dosya Sistemi Kullanımı

```
jag@os:~$ write test.txt Merhaba Dunya
Wrote 15 bytes to: test.txt

jag@os:~$ ls
Contents of /:
NAME             SIZE
test.txt         15 bytes

jag@os:~$ cat test.txt
=== File Contents ===
Merhaba Dunya

jag@os:~$ rm test.txt
Removed: test.txt
```

## Ortam Değişkeni Kullanımı

```
jag@os:~$ set PATH=/bin
Variable set.

jag@os:~$ env
Environment Variables:
PATH=/bin
```
