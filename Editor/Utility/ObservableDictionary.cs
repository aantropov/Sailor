﻿using System.Collections;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using YamlDotNet.Core.Events;
using YamlDotNet.Core;
using YamlDotNet.Serialization.NamingConventions;
using YamlDotNet.Serialization;

namespace SailorEditor.Utility
{
    [DebuggerDisplay("Count={Count}")]
    public class ObservableDictionary<TKey, TValue> : IDictionary<TKey, TValue>, INotifyCollectionChanged, INotifyPropertyChanged
    {
        readonly IDictionary<TKey, TValue> dictionary;

        public event NotifyCollectionChangedEventHandler CollectionChanged;
        public event PropertyChangedEventHandler PropertyChanged;

        public ObservableDictionary() : this(new Dictionary<TKey, TValue>()) { }
        public ObservableDictionary(IDictionary<TKey, TValue> dictionary) { this.dictionary = dictionary; }

        void AddWithNotification(KeyValuePair<TKey, TValue> item) { AddWithNotification(item.Key, item.Value); }
        void AddWithNotification(TKey key, TValue value)
        {
            dictionary.Add(key, value);

            CollectionChanged?.Invoke(this, new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add,
                new KeyValuePair<TKey, TValue>(key, value)));

            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Count"));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Keys"));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Values"));
        }

        bool RemoveWithNotification(TKey key)
        {
            TValue value;
            if (dictionary.TryGetValue(key, out value) && dictionary.Remove(key))
            {
                CollectionChanged?.Invoke(this, new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove,
                    new KeyValuePair<TKey, TValue>(key, value)));

                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Count"));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Keys"));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Values"));

                return true;
            }

            return false;
        }

        void UpdateWithNotification(TKey key, TValue value)
        {
            TValue existing;
            if (dictionary.TryGetValue(key, out existing))
            {
                dictionary[key] = value;

                CollectionChanged?.Invoke(this, new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Replace,
                    new KeyValuePair<TKey, TValue>(key, value),
                    new KeyValuePair<TKey, TValue>(key, existing)));

                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Values"));
            }
            else
            {
                AddWithNotification(key, value);
            }
        }

        protected void RaisePropertyChanged(PropertyChangedEventArgs args)
        {
            PropertyChanged?.Invoke(this, args);
        }

        #region IDictionary<TKey,TValue> Members

        public void Add(TKey key, TValue value)
        {
            AddWithNotification(key, value);
        }
        public bool ContainsKey(TKey key)
        {
            return dictionary.ContainsKey(key);
        }
        public ICollection<TKey> Keys
        {
            get { return dictionary.Keys; }
        }

        public bool Remove(TKey key)
        {
            return RemoveWithNotification(key);
        }

        public bool TryGetValue(TKey key, out TValue value)
        {
            return dictionary.TryGetValue(key, out value);
        }

        public ICollection<TValue> Values
        {
            get { return dictionary.Values; }
        }

        public TValue this[TKey key]
        {
            get { return dictionary[key]; }
            set { UpdateWithNotification(key, value); }
        }

        #endregion

        #region ICollection<KeyValuePair<TKey,TValue>> Members

        void ICollection<KeyValuePair<TKey, TValue>>.Add(KeyValuePair<TKey, TValue> item)
        {
            AddWithNotification(item);
        }

        void ICollection<KeyValuePair<TKey, TValue>>.Clear()
        {
            ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).Clear();

            CollectionChanged(this, new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));

            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Count"));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Keys"));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Values"));
        }

        bool ICollection<KeyValuePair<TKey, TValue>>.Contains(KeyValuePair<TKey, TValue> item)
        {
            return ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).Contains(item);
        }

        void ICollection<KeyValuePair<TKey, TValue>>.CopyTo(KeyValuePair<TKey, TValue>[] array, int arrayIndex)
        {
            ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).CopyTo(array, arrayIndex);
        }

        int ICollection<KeyValuePair<TKey, TValue>>.Count
        {
            get { return ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).Count; }
        }

        bool ICollection<KeyValuePair<TKey, TValue>>.IsReadOnly
        {
            get { return ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).IsReadOnly; }
        }

        bool ICollection<KeyValuePair<TKey, TValue>>.Remove(KeyValuePair<TKey, TValue> item)
        {
            return RemoveWithNotification(item.Key);
        }

        #endregion

        #region IEnumerable<KeyValuePair<TKey,TValue>> Members

        IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator()
        {
            return ((ICollection<KeyValuePair<TKey, TValue>>)dictionary).GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator() => dictionary.GetEnumerator();

        #endregion
    }

    public class ObservableDictionaryConverter<TKey, TValue> : IYamlTypeConverter
    {
        public ObservableDictionaryConverter(IYamlTypeConverter KeyConverter = null, IYamlTypeConverter ValueConverter = null)
        {
            keyConverter = KeyConverter;
            valueConverter = ValueConverter;
        }

        public bool Accepts(Type type) => type == typeof(ObservableDictionaryConverter<TKey, TValue>);

        public object ReadYaml(IParser parser, Type type)
        {
            var deserializerBuilder = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .IgnoreUnmatchedProperties()
                .IncludeNonPublicProperties();

            if (valueConverter != null)
                deserializerBuilder.WithTypeConverter(valueConverter);

            if (keyConverter != null)
                deserializerBuilder.WithTypeConverter(keyConverter);

            var deserializer = deserializerBuilder.Build();
            var dict = new ObservableDictionary<TKey, TValue>();

            parser.Consume<MappingStart>();

            while (parser.TryConsume<MappingEnd>(out var _))
            {
                var key = deserializer.Deserialize<TKey>(parser);
                parser.Consume<Scalar>();
                var value = deserializer.Deserialize<TValue>(parser);

                dict.Add(key, value);
            }

            return dict;
        }

        public void WriteYaml(IEmitter emitter, object value, Type type)
        {
            var dict = (ObservableDictionary<TKey, TValue>)value;
            var serializerBuilder = new SerializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance);

            if (valueConverter != null)
                serializerBuilder.WithTypeConverter(valueConverter);

            if (keyConverter != null)
                serializerBuilder.WithTypeConverter(keyConverter);

            var serializer = serializerBuilder.Build();

            emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));

            foreach (var kvp in dict)
            {
                serializer.Serialize(emitter, kvp.Key);
                serializer.Serialize(emitter, kvp.Value);
            }

            emitter.Emit(new MappingEnd());
        }

        IYamlTypeConverter valueConverter = null;
        IYamlTypeConverter keyConverter = null;
    }
}
