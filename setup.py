from distutils.core import setup, Extension

module1 = Extension('pymef2',
                    sources = ['pymef2.c'])

setup (name = 'pymef2',
       version = '1.0',
       author = "Hoameng Ung",
       author_email = "hoameng@blackfynn.com",
       description = 'Minimal Python Wrapper to MEF v2.0 C library.',
       ext_modules = [module1])