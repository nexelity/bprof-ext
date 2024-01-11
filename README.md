# 📊 BPROF - A PHP Profiling Extension 🚀

Uncover bottlenecks, memory hogs, and performance insights in your PHP code with Bprof! A heavy adaptation of the renowned [XHProf](https://github.com/phacility/xhprof) library, fine-tuned for modern PHP applications.

## 🌟 Features

- 🔍 Detailed function-level insights
- 📈 Real-time application performance monitoring
- 📊 Easy-to-visualize data
- ⚙️ Easy integration with [Laravel](https://github.com/nexelity/bprof-laravel/), Yii, and other PHP frameworks
- 🚀 Speed up your PHP applications!

## 🛠 Installation

### Linux and MacOS

```bash
curl https://github.com/nexelity/bprof-ext/archive/refs/tags/v1.3.tar.gz --silent --output bprof.tar.gz
tar -zxvf bprof.tar.gz
cd nexelity-bprof-ext-*
phpize && ./configure && make && make install
```

Add `extension=bprof.so` to your `php.ini`.

## 🎛 Usage

### Basic Usage

```php
<?php
bprof_enable();

// run your app here

$perfData = bprof_disable();
```

## 🤝 Contributing

1. Fork it ( https://github.com/nexelity/bprof-ext/fork )
2. Create your feature branch (`git checkout -b feature/fooBar`)
3. Commit your changes (`git commit -am 'Add some fooBar'`)
4. Push to the branch (`git push origin feature/fooBar`)
5. Create a new Pull Request

## ⚖️ License

MIT License. See the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments and Origins

- Bprof is a heavy adaptation of the defunct [XHProf](https://github.com/phacility/xhprof) library.
- All contributors and everyone who reported issues and gave feedback
