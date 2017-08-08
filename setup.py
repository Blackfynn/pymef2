from distutils.core import setup, Extension

module1 = Extension('pymef2',
                    sources = ['pymef2.c'])

setup (name = 'pymef2',
       version = '1.0',
       description = 'Python Wrapper to MEF C library.',
       ext_modules = [module1])