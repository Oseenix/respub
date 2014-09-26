#!/usr/bin/python
##
# @file stat_grp.py
# @brief Parse config file
# @author zhoujinze, zhoujz@chinanetcenter.com
# @version 0.0.1
# @date 2014-09-24

import configobj
import time
import IPy
import copy

import sys
import types

from IPy import IP, IPSet
from configobj import ConfigObj

if sys.version_info >= (3,):
    INT_TYPES = (int,)
    STR_TYPES = (str,)
    xrange = range
else:
    INT_TYPES = (int, long)
    STR_TYPES = (str, unicode)


global config
cfgfile = './config.txt'
config = ConfigObj(cfgfile)

# convert like "abc, cde, dcc, 10" -> ['abc', 'cde', 'ddc', 10]
def string2list(lstr):
  if (len(lstr) == 0):
    lstr = "[]"
  else:
    lstr = lstr[:-1] + lstr[-1] + '\']'
    #lstr = lstr.replace(lstr[-1], lstr[-1] + '\']', 1)
    lstr = lstr.replace(lstr[0],  '[\'' + lstr[0], 1)
    lstr = lstr.replace(",", '\', \'')
    #print '--------------', lstr
  lst = list(eval(lstr))
  return lst

def parse_agpstr(string):
  #convert to str
  agpstr = str(string)
  #strip space, all sutuation to one format
  agpstr = agpstr.replace(" ", "")
  agpstr = agpstr.replace("[", "")
  agpstr = agpstr.replace("]", "")
  agpstr = agpstr.replace("'", "")
  #print "RRR:", agpstr
  #find key domain
  index = agpstr.find('|')
  if index < 0:
    raise ValueError("Config do not have a right format!", str(string))
  acfg_dm = agpstr[:index]
  acfg_key = string2list(acfg_dm)

  #find value domain
  agpstr = agpstr[index + 1:]
  index = agpstr.find('|')
  if index < 0:
    raise ValueError("Config do not have a right format!", str(string))
  acfg_colname = agpstr[:index]
  acfg_colname = acfg_colname.replace(" ", "")
  acfg_stat_colname = string2list(acfg_colname)

  #find threshold
  acfg_thdstr = agpstr[index+1:]
  acfg_thds = string2list(acfg_thdstr)
  for idx, thd in enumerate(acfg_thds):
    if thd.isdigit() != True:
      raise ValueError("Config do not have a right format!", str(string), thd)
    acfg_thds[idx] = int(thd)

  return acfg_key, acfg_stat_colname, acfg_thds

##### Maybe there should had a class define every acfg_item ####
class acfg_entry():
  def __init__(self):
    self.acfg_name = ""
    self.acfg_dmlist = []
    self.acfg_dmname = []
    self.acfg_stat_cols = []
    self.acfg_stat_name = []
    self.acfg_threshold = []

class auto_cfg(object):
  def __init__(self, section, ksec):
    self.acfg = section
    self.alen = len(section)
    self.acfg_name = []
    self.acfg_stat_name= []
    self.acfg_stat_cols= []
    self.acfg_threshold = []
    self.acfg_kidx = []
    self.acfg_key = []

    for icfg, acfg_item in enumerate(self.acfg):
      #cfg-name
      print self.acfg[acfg_item]
      keylist, value, threshold = parse_agpstr(self.acfg[acfg_item])
      self.acfg_name.append(acfg_item)
      self.acfg_stat_name.append([])
      self.acfg_stat_cols.append([])
      self.acfg_threshold.append([])

      for v, t in zip(value, threshold):
        if v in ksec.keys():
          self.acfg_stat_name[icfg].append(v)
          self.acfg_stat_cols[icfg].append(ksec[v])
          self.acfg_threshold[icfg].append(int(t))
        else:
          raise ValueError("Config do not have a right format!", str(self.acfg[acfg_item]))

      self.acfg_kidx.append([])
      self.acfg_key.append([])

      for ikey, key in enumerate(keylist):
        try:
          #print ikey, key
          self.acfg_key[icfg].append(key)
          self.acfg_kidx[icfg].append(int(ksec[key]))
          pass
        except KeyError:
          raise ValueError("%r, Invalid config key! %r", key, ksec.keys())

      print "acfg:", icfg, self.acfg_name, self.acfg_key[icfg], self.acfg_kidx[icfg], \
          self.acfg_stat_name[icfg], self.acfg_stat_cols[icfg], self.acfg_threshold[icfg]

  def get_acfg_num(self):
    return self.alen

  def get_acfg_name(self, aidx):
    if adix >= self.alen:
      return None
    return self.acfg_name[aidx]

  def get_acfg_dmlist(self, aidx):
    if adix >= self.alen:
      return []
    return self.acfg_kidx[aidx]
  
  def get_acfg_dmname(self, aidx):
    if adix >= self.alen:
      return []
    return self.acfg_key[aidx]
  
  def get_acfg_stat_cols(self, aidx):
    if adix >= self.alen:
      return []
    return self.acfg_stat_cols[aidx]
  
  def get_acfg_stat_name(self, aidx):
    if adix >= self.alen:
      return []
    return self.acfg_stat_name[aidx]
  
  def get_acfg_threshold(self, aidx):
    if adix >= self.alen:
      return []
    return self.acfg_threshold[aidx]

  def get_acfg_item(self, aidx, acfg_item):
    if adix >= self.alen:
      return

    acfg_item.acfg_name = self.get_acfg_name(adix)
    acfg_item.acfg_dmlist = self.get_acfg_dmlist(adix)
    acfg_item.acfg_dmname = self.get_acfg_dmname(adix)
    acfg_item.acfg_stat_cols = self.get_acfg_stat_cols(adix)
    acfg_item.acfg_stat_name = self.get_acfg_stat_name(adix)
    acfg_item.acfg_threshold = self.get_acfg_threshold(adix)

    return
    
class spc_gcfg(object):
  def __init__(self, section, ksec):
    self.scfg = section
    self.slen = len(section)

    self.scfg_name = []
    self.scfg_keyidx = []
    self.scfg_keyname = []
    self.scfg_keymatch = []
    self.scfg_matchidx = []
    self.scfg_agp = []

    for iscfg , scfg_item in enumerate(self.scfg):
      self.scfg_name.append(scfg_item)
      self.scfg_keyidx.append([])
      self.scfg_keyname.append([])
      self.scfg_keymatch.append([])
      self.scfg_matchidx.append([])

      self.scfg_agp.append([])
      #first elem save group number
      self.scfg_agp[iscfg].append(0)

      print self.scfg[scfg_item]
      for iagp, agp_item in enumerate(self.scfg[scfg_item]):
        #print iagp, agp_item, self.scfg[scfg_item][agp_item]
        if agp_item in ksec.keys():
          try:
            self.scfg_keyname[iscfg].append(agp_item)
            self.scfg_keyidx[iscfg].append(int(ksec[agp_item]))
            self.scfg_keymatch[iscfg].append(self.scfg[scfg_item][agp_item])
          except:
            pass
        else:
          #auto group item
          self.scfg_agp[iscfg][0] += 1
          a_idx = self.scfg_agp[iscfg][0]
          
          keylist, colname_list, thrd_list = parse_agpstr(self.scfg[scfg_item][agp_item])
          if len(colname_list) != len(thrd_list):
            raise ValueError("Config do not have a right format!", \
                str(self.scfg[scfg_item][agp_item]))

          self.scfg_agp[iscfg].append([])
          #save agp cfg: name, value_name, value_col, threshold
          self.scfg_agp[iscfg][a_idx].append(agp_item)
          #stat col name
          self.scfg_agp[iscfg][a_idx].append([])
          #stat col 
          self.scfg_agp[iscfg][a_idx].append([])
          #stat threshold
          self.scfg_agp[iscfg][a_idx].append([])
          
          for v, t in zip(colname_list, thrd_list):
            if v in ksec.keys():
              self.scfg_agp[iscfg][a_idx][1].append(v)
              self.scfg_agp[iscfg][a_idx].append([])
              self.scfg_agp[iscfg][a_idx][2].append(int(ksec[v]))
              self.scfg_agp[iscfg][a_idx].append([])
              self.scfg_agp[iscfg][a_idx][3].append(int(t))
            else:
              raise ValueError("Config do not have a right format!", str(self.scfg[scfg_item][agp_item]))

          #keyidx list and keyname list
          self.scfg_agp[iscfg][a_idx].append([])
          self.scfg_agp[iscfg][a_idx].append([])
          self.scfg_agp[iscfg][a_idx].append([])

          # i don't know how to use maroc now, so 4/5...
          for ikey, key in enumerate(list(keylist)):
            try:
              self.scfg_agp[iscfg][a_idx][4].append(int(ksec[key]))
              self.scfg_agp[iscfg][a_idx][5].append(key)
            except:
              pass
            
          print 'scfgapc:', iscfg, a_idx, self.scfg_agp[iscfg][a_idx]          
        
      print 'scfg:', iscfg, self.scfg_keyname[iscfg], \
            self.scfg_keyidx[iscfg], self.scfg_keymatch[iscfg]

  def chk_spcagc_cfg(self, sidx):
    # check agpconfig item, agpkeyname should not contain any word in spcfg
    spc_dmlist = self.get_spc_dmlist(sidx)
    if len(spc_dmlist) < 1:
      return
    for a_idx in range(self.get_spcagc_num(sidx)):
      spcagc_dmlist = self.get_spcagc_dmlist(iscfg, a_idx - 1)
      spcagc_dmname = self.get_spcagc_dmname(iscfg, a_idx - 1)
      if len(spcagc_dmname) != len(spcagc_dmlist):
        raise ValueError("Config Error:", spcagc_dmname, spcagc_dmlist)
      for idx, akeyidx in enumerate(spcagc_dmlist):
        if akeyidx in self.get_spc_dmlist(sidx):
          del spcagc_dmlist[idx]
          del spcagc_dmname[idx]
          #delete and warning
          print "Agpcfg domain is dupplicated spcfg:", spcagc_dmname, self.get_spc_dmname(sidx)
      pass
    pass

  # get spcfg number
  def get_spcfg_num(self):
    return (self.slen)

  # get sidx-th spcfg's special part
  def get_spc_name(self, sidx):
    if sidx >= self.slen:
      return None
    return self.scfg_name
    
  def get_spc_dmlist(self, sidx):
    if sidx >= self.slen:
      return []    
    return self.scfg_keyidx[sidx]

  def get_spc_matchlist(self, sidx):
    if sidx >= self.slen:
      return []    
    return self.scfg_matchidx[sidx]

  def set_spc_matchlist(self, sidx, idxlist):
    if sidx >= self.slen:
      return []
    if len(idxlist) != len(self.scfg_keyidx[sidx]):
      print "Config convert Error:", self.scfg_keyname[sidx], self.scfg_keyidx[sidx], idxlist
    self.scfg_matchidx[sidx] = copy.deepcopy(idxlist)
    return

  def get_spc_dmmatch(self, sidx):
    if sidx >= self.slen:
      return []    
    return self.scfg_keymatch[sidx]

  def get_spc_dmname(self, sidx):
    if sidx >= self.slen:
      return []    
    return self.scfg_keyname[sidx]

  def get_spcagc_num(self, sidx):
    if sidx >= self.slen:
      return 0
    return self.scfg_agp[sidx][0]

  def get_spcagc_name(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 >= self.get_spcagc_num(sidx):
      return None
    return self.scfg_agp[sidx][aidx + 1][0]
 
  def get_spcagc_dmlist(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return []
    return self.scfg_agp[sidx][aidx + 1][4]
    
  def get_spcagc_dmname(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return []
    return self.scfg_agp[sidx][aidx + 1][5]
  
  def get_spcagc_statcol(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return []
    return self.scfg_agp[sidx][aidx + 1][2]

  def get_spcagc_stat_name(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return []
    return self.scfg_agp[sidx][aidx + 1][1]

  def get_spcagc_threshold(self, sidx, aidx):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return []
    return self.scfg_agp[sidx][aidx + 1][3]
  
  def get_spcagc_item(self, sidx, aidx, acfg_item):
    if sidx >= self.slen or aidx + 1 > self.get_spcagc_num(sidx):
      return False
    acfg_item.acfg_name = self.get_spcagc_name(sidx, adix)
    acfg_item.acfg_dmlist = self.get_spcagc_dmlist(sidx, adix)
    acfg_item.acfg_dmname = self.get_spcagc_dmname(sidx, adix)
    acfg_item.acfg_stat_cols = self.get_spcagc_stat_cols(sidx, adix)
    acfg_item.acfg_stat_name = self.get_spcagc_stat_name(sidx, adix)
    acfg_item.acfg_threshold = self.get_spcagc_threshold(sidx, adix)

    return True

def getcfg(filename):
  return config

def parse_cfg(filename):
  sec_path = config['path']
  sec_opt = config['log-format']
  sec_agp = config['auto-grp']
  sec_sgp = config['spc-grp']
  return sec_path, sec_opt, sec_agp, sec_sgp

if __name__ == "__main__":
  sec_path, sec_opt, sec_agp, sec_sgp = parse_cfg('./config.txt')
  spc = spc_gcfg(sec_sgp, sec_opt)
  print '================='
  agc = auto_cfg(sec_agp, sec_opt)
