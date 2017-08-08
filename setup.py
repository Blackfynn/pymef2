from distutils.core import setup, Extension

module1 = Extension('pymeflib',
                    sources = ['pymeflib.c'])

setup (name = 'pymeflib',
       version = '1.0',
       description = 'Python Wrapper to MEF C library.',
       ext_modules = [module1])